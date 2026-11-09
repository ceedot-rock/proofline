# Contributing to proofline

## Flow

1. Open an issue or pick an existing one.
2. Branch from the default branch (`main`): `fix/short-name`.
3. Keep diffs small and focused; one concern per pull request.
4. Open a pull request against `main` using the template. CI must pass.
5. A maintainer merges. Never push directly to `main`.

## Checks

Run `make test` from the repo root. CI must pass.

## Secrets

Never commit API keys, tokens, or credentials. If you leak one, rotate it
immediately and say so in the pull request.

## License

By contributing you agree your work is distributed under the repo's LICENSE.
