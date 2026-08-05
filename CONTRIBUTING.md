# Contributing

Thank you for considering contributing to this project!

## Quick Start

1. Fork the repository
2. Create a feature branch: `git switch -c feature/my-feature`
3. Make your changes
4. Run the full-checkout validation listed under README **Running Tests**,
   including native tests, repository guards, both Arduino targets, Doxygen,
   and package inspection. Run the native ESP-IDF matrix when its toolchain is
   available.
5. Commit with a clear message: `git commit -m "feat: add X"`
6. Push and open a Pull Request

## Guidelines

### Code Style
- Follow existing code style (see `.clang-format`)
- Use `constexpr` instead of macros for constants
- Prefer explicit over implicit
- No heap allocations in steady-state library code

### Commits
- Use [Conventional Commits](https://www.conventionalcommits.org/) format:
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation only
  - `refactor:` code change that neither fixes a bug nor adds a feature
  - `test:` adding or updating tests
  - `chore:` maintenance tasks

### Pull Requests
- Keep PRs focused (one feature/fix per PR)
- Update documentation if needed
- Add changelog entry under `[Unreleased]`
- Ensure CI passes
- Do not commit generated build/package/Doxygen output. Preserve vendor source
  documents. Archive accepted raw hardware transcripts outside the checkout and
  keep only stable fingerprints and claim boundaries in maintained docs.

## Release Procedure

Prepare and review the release on a branch; do not tag an unmerged branch.

```bash
git switch main
git pull --ff-only origin main
git switch -c agent/release-X.Y.Z
python scripts/generate_version.py set X.Y.Z
python scripts/generate_version.py check
```

Move the accumulated changelog entries from `[Unreleased]` to a dated
`[X.Y.Z]` section, update supported-version and current-version documentation,
then run the complete README **Running Tests** matrix. Commit, push, open the
release-prep pull request, and require all GitHub Actions jobs to pass.

After that pull request is merged, tag the exact updated `main` commit:

```bash
git switch main
git pull --ff-only origin main
git status --short
python scripts/generate_version.py check
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin vX.Y.Z
```

`git status --short` must print nothing. On GitHub, open **Releases**, choose
**Draft a new release**, select the pushed `vX.Y.Z` tag, use `SHT3x vX.Y.Z` as
the title, paste the matching changelog section, and publish it as the latest
non-prerelease release.

### What We Accept
- Bug fixes
- Documentation improvements
- Performance improvements (with benchmarks)
- New examples (if they demonstrate a common use case)

### What We Probably Won't Accept
- Breaking API changes without discussion
- Heavy dependencies
- Platform-specific code in the library core
- Features that add heap allocations in steady state

## Questions?

Open a GitHub Discussion or Issue for questions.
