# Todo

- Set "elysia_cron_disabled" variable to FALSE if elysia_cron is installed
- Check if the cdn module should be disabled in all modes (currently it is disabled only in --dev mode), or should some action be taken to make it work safe
- Create `fix-domains` command (request from Bastian)
- Replace `--restore-sas` option with `--sapi` (Search API). Provide some modes:
  - `readonly`: just set all indexes to readonly mode
  - `restore`: restore Search API servers settings (as `--restore-sas` currently does)
  - `restore-reindex`: the same + reindex in background

# Drush safe-sync command

Helps keep staging/dev servers in actual state.

Helps update/sync local sites from dev/staging/live servers.

See `drush help safe-sync` for more information.

**Tested only with amazee Drupal 7 installations.**

## Installation

    git clone git@github.com:AmazeeLabs/drush_safe_sync.git ~/.drush/drush_safe_sync && drush cc drush
