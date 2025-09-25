# Development Guardrails (Condensed)

- Use `timeout 60 …` (or stricter) whenever running `npm test`, `jest`, or other long-lived commands.
- Always wrap ad-hoc Node scripts or tests with `timeout` (e.g., `timeout 30 node --input-type=module …`).
- Reuse existing build/test scripts where possible; avoid ad-hoc command invocations without a timeout guard.
- The standard toolchain remains: `./build.sh` for WASM, `timeout 60 npm test --workspace=@m68k/core` for core tests, `npm run build` / `npm run typecheck` for TypeScript.
- You may use the GitHub CLI (`gh`) to create pull requests and check GitHub Actions status from the terminal—preferred when branch protection blocks direct pushes.
- To verify npm-package exports, rely on `run-tests-ci.sh`; it builds the bundle and runs `npm-package/test/integration.mjs`, which is the canonical smoke test for published entrypoints.

## Release Checklist (0.1.x)

1. Merge release changes to `master`, but leave `npm-package/package.json` on the last published version (the workflow bumps it).
2. Cut changelog/docs updates in git as needed—no version bump commit.
3. Create an annotated tag locally (`git tag -a vX.Y.Z -m "Release X.Y.Z"`) and push it (`git push origin vX.Y.Z`).
4. The publish workflow (auto-triggered by the tag) reuses CI artifacts, runs `npm version` inside `npm-package/`, and publishes to npm.
5. Monitor the workflow: ensure “Publish to npm” succeeds (it exits early if the version already exists).
6. Update GitHub release notes if needed and verify the package on npm.
