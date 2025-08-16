module.exports = {
  root: true,
  parser: '@typescript-eslint/parser',
  plugins: [
    '@typescript-eslint',
  ],
  extends: [
    'eslint:recommended',
  ],
  parserOptions: {
    ecmaVersion: 2020,
    sourceType: 'module',
  },
  env: {
    node: true,
    es2020: true,
  },
  rules: {
    // Allow unused variables that start with underscore
    '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
    
    // Allow explicit any when needed
    '@typescript-eslint/no-explicit-any': 'warn',
    
    // Enforce consistent type imports (disabled for simplicity)
    // '@typescript-eslint/consistent-type-imports': 'error',
    
    // Prefer const over let
    'prefer-const': 'error',
    
    // No console.log in production code (allow console.error and console.warn)
    'no-console': ['warn', { allow: ['warn', 'error'] }],
    
    // Enforce semicolons
    'semi': ['error', 'always'],
    
    // Enforce single quotes
    'quotes': ['error', 'single', { avoidEscape: true }],
    
    // Disable rules that are handled by TypeScript
    'no-undef': 'off',
    'no-unused-vars': 'off',
  },
  overrides: [
    {
      // Test files can be more lenient
      files: ['**/*.test.ts', '**/*.spec.ts'],
      rules: {
        '@typescript-eslint/no-explicit-any': 'off',
        'no-console': 'off',
      },
    },
  ],
};