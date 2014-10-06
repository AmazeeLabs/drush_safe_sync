<?php

/**
 * @file
 * Functions for the "safe-sync" command.
 */

/**
 * Loads alias. If group is provided: loads one random alias from the group.
 *
 * @param string $alias
 *
 * @return array
 *   array({alias-name}, {alias-record})
 *
 * @throws Exception
 */
function _safe_sync_extract_alias($alias) {
  $alias_record = drush_sitealias_get_record($alias);
  if (!$alias_record) {
    throw new Exception('Site alias or group of site aliases is not valid.');
  }
  if (!empty($alias_record['site-list'])) {
    $alias = $alias_record['site-list'][array_rand($alias_record['site-list'])];
    $alias_record = drush_sitealias_get_record($alias);
    if (!$alias_record) {
      throw new Exception($alias . ' site alias randomly loaded from the given group of site aliases is not valid.');
    }
  }
  return array($alias, $alias_record);
}

/**
 * Synchronizes database.
 *
 * @throws Exception
 */
function _safe_sync_db($alias, $alias_record) {
  _safe_sync_color_echo('Downloading database...');
  if (!drush_shell_exec("drush sql-sync $alias default -y")) {
    // The most popular problems here are the domain module or empty database:
    // drush needs the lowest bootstrap level to download the database from
    // remote site, however, when it tries to import dump, it bootstraps to
    // database level, and if some module (like domain) tries to select
    // something from an empty database, the error is thrown.
    // The good news is that DB dump is already downloaded and saved to the
    // temporary directory.
    // Try to detect that case and import dump "manually".
    $output = drush_shell_exec_output();
    $temp = drush_find_tmp();
    if (preg_match_all('#' . preg_quote($temp, '#') . '/.+?.sql.gz#', implode(' ', $output), $matches)) {
      $local_files = array_filter($matches[0], 'file_exists');
      if (!empty($local_files)) {
        _safe_sync_import_dump(array_pop($local_files));
      }
      else {
        throw new Exception('Cannot sync database: cannot find downloaded dump file.');
      }
    }
    elseif (preg_match_all('#/[^ ]+?.sql.gz#', implode(' ', $output), $matches)) {
      $user = $alias_record['remote-user'];
      $host = $alias_record['remote-host'];
      $remote_file = $matches[0][0];
      $temp_file = drush_tempnam('', NULL, '.sql.gz');
      if (!drush_shell_exec("scp $user@$host:$remote_file $temp_file")) {
        throw new Exception('Cannot download database dump.');
      }
      _safe_sync_import_dump($temp_file);
      unlink($temp_file);
    }
    else {
      throw new Exception('Cannot sync database.');
    }
  }

  _safe_sync_color_echo('Cleaning database...');
  $result = drush_invoke_process('@self', 'sql-query', array('SHOW TABLES'), array(), array('output' => FALSE));
  $local_tables = array_filter(array_map('trim', explode("\n", $result['output'])));
  $result = drush_invoke_process($alias_record, 'sql-query', array('SHOW TABLES'), array(), array('output' => FALSE));
  $remote_tables = array_filter(array_map('trim', explode("\n", $result['output'])));
  $diff = array_diff($local_tables, $remote_tables);
  if (!empty($diff)) {
    drush_invoke_process('@self', 'sql-query', array('DROP TABLES ' . implode(', ', $diff)), array(), array('output' => FALSE));
  }
}

/**
 * Imports .sql.gz dump file.
 *
 * @param string $filepath
 *
 * @throws Exception
 */
function _safe_sync_import_dump($filepath) {
  $filepath = drush_escapeshellarg($filepath);
  drush_sql_conf(); // Init SQL class suppressing possible errors.
  $sql = drush_sql_get_class();
  if (!$sql->db_exists()) {
    _safe_sync_color_echo('Creating database...');
    // Save DB spec first, because it can be changed.
    $db_spec = $sql->db_spec;
    $sql->createdb();
    $sql->db_spec = $db_spec;
    if (!$sql->db_exists()) {
      throw new Exception('Cannot create database.');
    }
  }
  _safe_sync_color_echo('Drush sql-sync did not finished import because of errors, but database dump was downloaded. Importing it...');
  $credentials = $sql->creds(FALSE);
  if (!drush_shell_exec("gunzip < $filepath | mysql $credentials")) {
    throw new Exception('Dump import failed.');
  }
}

/**
 * Prints to console.
 */
function _safe_sync_color_echo($text, $prefix = '') {
  if ($prefix) {
    // Green bold.
    echo "\033[1;33m$prefix\033[0m";
  }
  // Orange bold.
  echo "\033[1;32m$text\033[0m
";
}
