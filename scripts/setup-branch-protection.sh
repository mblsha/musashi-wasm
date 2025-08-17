#!/bin/bash

# Setup script for GitHub branch protection on musashi-wasm repository
# This script applies the branch protection rules defined in branch-protection.json

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "GitHub Branch Protection Setup for musashi-wasm"
echo "================================================"
echo ""

# Check if gh CLI is installed
if ! command -v gh &> /dev/null; then
    echo -e "${RED}Error: GitHub CLI (gh) is not installed.${NC}"
    echo "Please install it from: https://cli.github.com/"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    echo -e "${RED}Error: Not authenticated with GitHub.${NC}"
    echo "Please run: gh auth login"
    exit 1
fi

# Repository information
REPO="mblsha/musashi-wasm"
BRANCH="master"
CONFIG_FILE="branch-protection.json"

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}Error: $CONFIG_FILE not found.${NC}"
    echo "Please ensure the configuration file exists in the repository root."
    exit 1
fi

echo "Repository: $REPO"
echo "Branch: $BRANCH"
echo "Config: $CONFIG_FILE"
echo ""

# Check current protection status
echo "Checking current branch protection status..."
if gh api "repos/$REPO/branches/$BRANCH/protection" &> /dev/null; then
    echo -e "${YELLOW}Branch protection already exists. It will be updated.${NC}"
    METHOD="PUT"
else
    echo "No existing branch protection found. It will be created."
    METHOD="PUT"
fi
echo ""

# List required status checks
echo "The following status checks will be required:"
echo "  - build-all-targets (Native CI)"
echo "  - sanitizer-tests (Native CI)"
echo "  - perfetto-build (Native CI)"
echo "  - wasm-build (WebAssembly CI)"
echo "  - wasm-perfetto-build (WebAssembly CI)"
echo ""

# Confirm with user
read -p "Do you want to apply these branch protection rules to $BRANCH? (y/N) " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted by user."
    exit 0
fi

echo ""
echo "Applying branch protection rules..."

# Apply the protection rules
if gh api "repos/$REPO/branches/$BRANCH/protection" \
    --method "$METHOD" \
    --input "$CONFIG_FILE"; then
    echo -e "${GREEN}✓ Branch protection rules applied successfully!${NC}"
else
    echo -e "${RED}✗ Failed to apply branch protection rules.${NC}"
    echo ""
    echo "Common issues and solutions:"
    echo "1. Status checks not found: Create a PR first to trigger the workflows"
    echo "2. Permission denied: Ensure you have admin access to the repository"
    echo "3. Invalid configuration: Check the JSON syntax in $CONFIG_FILE"
    echo ""
    echo "You can also try applying the rules through the GitHub web interface:"
    echo "https://github.com/$REPO/settings/branches"
    exit 1
fi

echo ""
echo "Branch protection has been configured with:"
echo "  ✓ Require pull request reviews (1 approval)"
echo "  ✓ Dismiss stale PR approvals on new commits"
echo "  ✓ Require status checks to pass"
echo "  ✓ Require branches to be up to date"
echo "  ✓ Include administrators"
echo "  ✓ Restrict force pushes"
echo "  ✓ Restrict branch deletion"
echo "  ✓ Require conversation resolution"
echo ""
echo -e "${GREEN}Setup complete!${NC}"
echo ""
echo "Next steps:"
echo "1. Create a test PR to verify the protection rules"
echo "2. Consider adding a CODEOWNERS file for automatic review assignments"
echo "3. Update README.md with contribution guidelines"
echo ""
echo "To view the current protection status:"
echo "  gh api repos/$REPO/branches/$BRANCH/protection | jq"
echo ""
echo "To remove protection (if needed):"
echo "  gh api repos/$REPO/branches/$BRANCH/protection --method DELETE"