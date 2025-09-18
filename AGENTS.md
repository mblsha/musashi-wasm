# Development Guardrails (Condensed)

- Use `timeout 60 …` (or stricter) whenever running `npm test`, `jest`, or other long-lived commands.
- If you run Node scripts from the command line for debugging, wrap them with `timeout` as well (e.g., `timeout 30 node --input-type=module …`).
- Reuse existing build/test scripts where possible; avoid ad-hoc command invocations without a timeout guard.
- The standard toolchain remains: `./build.sh` for WASM, `timeout 60 npm test --workspace=@m68k/core` for core tests, `npm run build` / `npm run typecheck` for TypeScript.
- You may use the GitHub CLI (`gh`) to create pull requests and check GitHub Actions status from the terminal—preferred when branch protection blocks direct pushes.
