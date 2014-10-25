# Todo

- Realtime output result of `drush sql-sync` (http://stackoverflow.com/a/6144213/580371)
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
