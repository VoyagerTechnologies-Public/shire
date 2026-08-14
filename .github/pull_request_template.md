## Summary

<!-- Describe the changes in this pull request. -->

## Regression Test

Prior to merging, all workflow checks for the pull request should be passing.
In addition to the automated checks, run a minimal set of builds and verifications:

- [ ] `make cli`
  - [ ] `make cli-start`
  - [ ] Confirm connections and basic cmd/tlm
- [ ] `make`
  - [ ] `make start`
  - [ ] Confirm no errors in startup log
  - [ ] Confirm YAMCS checkout stack passes without error
- [ ] `make test-fsw`
- [ ] `make test-sim`
- [ ] Self review of files changed

## Related

<!-- List related issues and any submodule pull requests that must be merged and updated here. -->

Closes #
