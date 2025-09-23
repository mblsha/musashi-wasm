declare module 'musashi-wasm/node' {
  export interface MusashiNodeInitOptions {
    locateFile?: (path: string, prefix?: string) => string;
    [key: string]: unknown;
  }

  export type MusashiNodeFactory = (options?: MusashiNodeInitOptions) => Promise<unknown>;

  const init: MusashiNodeFactory;

  export default init;
}
