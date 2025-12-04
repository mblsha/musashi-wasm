const TRUTHY_ENV_VALUES = new Set(['1', 'true', 'yes', 'on']);

export const parseBooleanEnv = (value: string | undefined): boolean => {
  if (!value) {
    return false;
  }

  return TRUTHY_ENV_VALUES.has(value.trim().toLowerCase());
};
