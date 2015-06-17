<?php

/**
 * Implements hook_drush_command().
 */
function safe_sync_drush_command() {
  $items = array();

  $items['safe-sync'] = array(
    'description' => '
Safely synchronizes site.

If alias is provided, the command will synchronize database. If group of aliases is provided, the command will use random alias from the group.
This works even if database is empty and domain module is enabled (sql-sync does not work with this conditions).
If database does not exist, the command will try to create it.

Without any option provided the command will:
  - enable emails reroute (only if mail system is not set to DevelMailLog and http://confluence.amazeelabs.com/display/KNOWLEDGE/Save+outgoing+emails+locally is not used)
  - make all search indexes readonly
  - correct domains/domain-variants basing on the configured $base_url
',
    'options' => array(
      'dev' => array(
        'description' => 'Enable development mode. With this option the command will additionally:
  - enable devel module
  - grant "access devel information" to all roles
  - disable page caching and js/css aggregation
  - disable the following modules:
    - varnish
    - redis
    - memcache
    - memcache_admin
    - update
    - admin_languaged
    - securepages
',
      ),
      'files' => array(
        'description' => 'Synchronize files in background. Only work when alias is provided.',
      ),
      'git-modules' => array(
        'description' => 'Sync/init/update git submodules.',
      ),
      'restore-sas' => array(
        'description' => 'Restore search_api server settings.',
      ),
    ),
    'examples' => array(
      'git pull && drush safe-sync @live --files --git-modules' => 'Update staging from production.',
      'drush safe-sync @dev --dev && drush uli' => 'Update from dev server with dev mode, then login.',
      'drush safe-sync' => 'Make the current installation "safe".',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_SITE,
  );

  $items['fix-domains'] = array(
    'description' => 'Updates domains and domain variants. Compatible with Drupal 6 and 7.',
    'examples' => array(
      'drush fix-domains site.lo' => 'Will update main domain and domain variants to "site.lo"',
      'drush fix-domains' => 'Will try to detect the domain from the $base_url and then update main domain and domain variants',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_DATABASE,
  );

  return $items;
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_safe_sync($alias = NULL) {

  if (drush_get_option('git-modules')) {
    _safe_sync_color_echo('Updating git submodules...');
    chdir(exec('git rev-parse --show-toplevel'));
    // Suppress standard output, but allow errors printing.
    exec('git submodule sync >/dev/null');
    exec('git submodule update --init >/dev/null');
    chdir(drush_get_context('DRUSH_DRUPAL_ROOT'));
  }

  // @todo: try to save search_api servers to restore them later.

  if ($alias) {
    list($alias, $alias_record) = _safe_sync_extract_alias($alias);

    if (drush_get_option('restore-sas')) {
      // Try to save search_api servers to restore them later.
      try {
        $cmd = 'drush eval \'var_export(db_select("search_api_server", "sas")->fields("sas", array("machine_name", "options"))->execute()->fetchAllKeyed());\' 2>/dev/null';
        $ret = shell_exec($cmd);
        if (strstr($ret, 'array')) {
          eval('$search_api_servers = ' . $ret . ';');
        }
      }
      catch (Exception $e) {
      }
    }

    if (drush_get_option('files')) {
      _safe_sync_color_echo('Synchronizing files in background...');
      $command = 'drush -y rsync ' . $alias . ':%files %files >/dev/null 2>/dev/null &';
      if (drush_get_context('DRUSH_VERBOSE')) {
        drush_print('Executing: ' . $command, 0, STDERR);
      }
      exec($command);
    }

    _safe_sync_db($alias, $alias_record);
  }

  // Suppress domain module errors.
  $GLOBALS['conf']['domain_hide_errors'] = TRUE;

  drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL);

  $devel_mail = (isset($GLOBALS['conf']['mail_system']['default-system']) && $GLOBALS['conf']['mail_system']['default-system'] === 'DevelMailLog');
  $output = array();
  $code = NULL;
  exec('cat "$(which sendmail)" | grep "file_put_contents("', $output, $code);
  $local_mail = (!empty($output) && $code === 0);
  if (!$devel_mail && !$local_mail) {
    _safe_sync_color_echo('Configuring email reroute...');
    drush_shell_exec('drush en reroute_email -y');
    variable_set('reroute_email_address', 'development@amazee.com');
    variable_set('reroute_email_enable', TRUE);
    variable_set('reroute_email_enable_message', TRUE);
  }

  if (module_exists('apachesolr') || module_exists('search_api')) {
    _safe_sync_color_echo('Making search indexes read-only...');
    $affected = 0;
    try {
      if (!empty($search_api_servers)) {
        foreach ($search_api_servers as $machine_name => $options) {
          db_update('search_api_server')
            ->condition('machine_name', $machine_name)
            ->fields(array('options' => $options))
            ->execute();
        }
        _safe_sync_color_echo('Search API servers was restored from previous configuration!', 'PLEASE NOTE: ');
      }
      $affected = db_update('search_api_index')
        ->fields(array('read_only' => 1))
        ->execute();
    } catch (Exception $s) {
    }
    if (function_exists('apachesolr_load_all_environments') && function_exists('apachesolr_environment_save')) {
      // The case if the apachesolr module is used.
      foreach (apachesolr_load_all_environments() as $env) {
        $env['conf']['apachesolr_read_only'] = APACHESOLR_READ_ONLY;
        apachesolr_environment_save($env);
        $affected++;
      }
    }
    if ($affected && !drush_get_option('dev')) {
      _safe_sync_color_echo('all search indexes have been set to readonly mode!', 'PLEASE NOTE: ');
    }
  }

  if (module_exists('domain_variants') || module_exists('domain')) {
    _safe_sync_color_echo('Correcting domains and domain variants...');
    // This is not really precise code... It works "intuitively" basing on our
    // usual practices.
    try {
      if (($domain = preg_replace('#^https?://#', '', $GLOBALS['base_url'])) && ($domain != 'default')) {
        // Usually, only the default (the first one) domain is used.
        db_update('domain')
          ->condition('is_default', 1)
          ->fields(array('subdomain' => $domain))
          ->execute();
        $variants = db_select('domain_variants', 'dv')
          ->fields('dv', array('variant_id', 'path'))
          ->execute()
          ->fetchAllKeyed();
        foreach ($variants as $variant_id => $path) {
          db_update('domain_variants')
            ->condition('variant_id', $variant_id)
            ->fields(array('path' => preg_replace('#^[^/]+#', $domain, $path)))
            ->execute();
        }
      }
      else {
        _safe_sync_color_echo('there is no $base_url set, domains/domain-variants were not updated!', 'PLEASE NOTE: ');
      }
    } catch (Exception $s) {}
  }

  if (module_exists('commerce_saferpay')) {
    _safe_sync_color_echo('Setting Commerce Saferpay to test account...');
    $affected = 0;
    try {
      $rules = db_select('rules_config', 'rc')
        ->condition('rc.name', 'commerce_payment_commerce_saferpay_%', 'LIKE')
        ->fields('rc', array('name'))
        ->execute()
        ->fetchCol();
      foreach ($rules as $rule_name) {
        /* @var RulesReactionRule $rule */
        $rule = rules_config_load($rule_name);
        /* @var RulesAction $action */
        foreach ($rule->actions() as $action) {
          if (isset($action->settings['payment_method']['method_id'])
              && strpos($action->settings['payment_method']['method_id'], 'commerce_saferpay_') === 0
              && isset($action->settings['payment_method']['settings']['account_id'])
              && $action->settings['payment_method']['settings']['account_id'] != '99867-94913159') {
            $action->settings['payment_method']['settings']['account_id'] = '99867-94913159';
            $action->settings['payment_method']['settings']['password'] = 'XAjc3Kna';
            $action->save();
            $affected++;
          }
        }
      }
    } catch (Exception $e) {}
    if ($affected && !drush_get_option('dev')) {
      _safe_sync_color_echo('Commerce Saferpay payment method now uses test account!', 'PLEASE NOTE: ');
    }
  }

  if (drush_get_option('dev')) {

    _safe_sync_color_echo('Ensuring devel is enabled...');
    $devel_enabled = FALSE;
    try {
      $devel_enabled = db_select('system', 's')
        ->condition('s.name', 'devel')
        ->fields('s', array('status'))
        ->execute()
        ->fetchField();
    }
    catch (Exception $e) {}
    if (!$devel_enabled) {
      drush_shell_exec('drush en devel -y');
    }

    _safe_sync_color_echo('Allowing all users to view devel info...');
    foreach (array(DRUPAL_ANONYMOUS_RID, DRUPAL_AUTHENTICATED_RID) as $rid) {
      db_merge('role_permission')
        ->key(array('rid' => $rid, 'permission' => 'access devel information'))
        ->fields(array('rid' => $rid, 'permission' => 'access devel information', 'module' => 'devel'))
        ->execute();
    }

    _safe_sync_color_echo('Disabling all possible caches...');
    variable_set('cache', 0);
    variable_set('block_cache', 0);
    variable_set('preprocess_css', 0);
    variable_set('preprocess_js', 0);

    _safe_sync_color_echo('Disabling modules which are useless for development...');
    module_disable(array(
      'varnish',
      'redis',
      'memcache',
      'memcache_admin',
      'update',
      'admin_language',
      'securepages',
      'cdn',
    ));
  }

  _safe_sync_color_echo('Clearing caches...');
  // Sometimes after update cache-clear don't work because broken cache raise
  // errors. If current cache provider is database, the following can help.
  drush_shell_exec('Q=\'\'; drush sql-query \'SHOW TABLES\' | grep cache | { while read table; do Q=$Q"TRUNCATE TABLE $table; "; done; drush sql-query "$Q"; }');
  drush_shell_exec('drush cc all');

  _safe_sync_color_echo('DONE!');
}

/**
 * Implements drush_hook_COMMAND().
 */
function drush_safe_sync_fix_domains($new_domain = NULL) {
  if (empty($new_domain)) {
    if (in_array($GLOBALS['base_url'], array('http://default', 'https://default'))) {
      drush_set_error('fix-domains', 'Cannot guess the domain because the $base_url is not set in settings.pnp! Please either set the $base_url, or use fix-domains command with explicit domain, example: drush fix-domains new-domain.com');
      return FALSE;
    }
    $new_domain = preg_replace('#(^https?://)|(/+$)#', '', $GLOBALS['base_url']);
  }
  $drupal_version = drush_drupal_major_version();
  if ($drupal_version == 7) {
    $old_domain = db_select('domain', 'd')
      ->fields('d', array('subdomain'))
      ->condition('d.is_default', 1)
      ->execute()
      ->fetchField();

  }
  elseif ($drupal_version == 6) {
    $old_domain = db_result(db_query("SELECT subdomain FROM {domain} WHERE domain_id = 0"));
  }
  else {
    drush_set_error('fix-domains', 'Cannot work with Drupal version ' . $drupal_version . '!');
    return FALSE;
  }
  if (drush_confirm("This will replace $old_domain with $new_domain. Are you sure you want to continue?")) {
    if ($drupal_version == 7) {
      db_update('domain')
        ->fields(array('subdomain' => $new_domain))
        ->condition('is_default', 1)
        ->execute();
      try {
        db_query("
          UPDATE {domain_variants}
          SET path = REPLACE(path, :old_domain, :new_domain)
          WHERE path LIKE :old_like
        ", array(
          ':old_domain' => $old_domain,
          ':new_domain' => $new_domain,
          ':old_like' => '%' . db_like($old_domain) . '%',
        ));
      }
      catch (Exception $e) {}
    }
    elseif ($drupal_version == 6) {
      db_query("UPDATE {domain} SET subdomain = '%s' WHERE domain_id = 0");
      try {
        db_query("
          UPDATE {domain_variants}
          SET path = REPLACE(path, '%s', '%s')
          WHERE path LIKE '%%%s%%'
        ", $old_domain, $new_domain, $old_domain);
      }
      catch (Exception $e) {}
    }
  }
  drupal_set_message("Replaced $old_domain with $new_domain successfully.");
}
