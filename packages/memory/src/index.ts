// Minimal system surface needed for memory helpers. Avoids coupling to @m68k/core
export interface SystemMemoryIO {
  readBytes(address: number, length: number): Uint8Array;
  writeBytes(address: number, data: Uint8Array): void;
}

/** A user-defined function that parses a byte buffer into a structured object. */
export type Parser<T> = (data: Uint8Array) => T;

/**
 * Provides structured, type-safe access to a single data structure
 * at a fixed address in the system's memory.
 */
export class MemoryRegion<T> {
  constructor(
    private readonly system: SystemMemoryIO,
    private readonly address: number,
    private readonly size: number,
    private readonly parser: Parser<T>
  ) {}

  /** Reads the memory region and returns the parsed structure. */
  public get(): T {
    const bytes = this.system.readBytes(this.address, this.size);
    return this.parser(bytes);
  }

  /** Writes raw bytes to the memory region. */
  public setBytes(data: Uint8Array): void {
    if (data.length !== this.size) {
      throw new Error(
        `Data size (${data.length}) does not match region size (${this.size})`
      );
    }
    this.system.writeBytes(this.address, data);
  }
}

/**
 * Provides array-like access to a collection of fixed-size structures in memory,
 * such as a game's entity list.
 */
export class MemoryArray<T> {
  constructor(
    private readonly system: SystemMemoryIO,
    private readonly baseAddress: number,
    private readonly stride: number, // The size of each element in bytes.
    private readonly parser: Parser<T>
  ) {}

  /** Reads and parses the element at the given index. */
  public at(index: number): T {
    const address = this.baseAddress + index * this.stride;
    const bytes = this.system.readBytes(address, this.stride);
    return this.parser(bytes);
  }

  /** Writes raw bytes to the element at the given index. */
  public setAt(index: number, data: Uint8Array): void {
    if (data.length !== this.stride) {
      throw new Error(
        `Data size (${data.length}) does not match stride (${this.stride})`
      );
    }
    const address = this.baseAddress + index * this.stride;
    this.system.writeBytes(address, data);
  }

  /** Creates an iterable over a range of indices. */
  public *iterate(start: number, count: number): IterableIterator<T> {
    for (let i = start; i < start + count; i++) {
      yield this.at(i);
    }
  }
}

/**
 * Helper utilities for parsing common data types from byte arrays.
 */
export class DataParser {
  private static ensureAvailable(
    data: Uint8Array,
    offset: number,
    requiredLength: number
  ): void {
    if (!Number.isInteger(offset)) {
      throw new RangeError(`Offset ${offset} must be an integer`);
    }
    if (offset < 0) {
      throw new RangeError(`Offset ${offset} is out of bounds for length ${data.length}`);
    }
    if (requiredLength < 0) {
      throw new RangeError('Required length must be non-negative');
    }
    if (offset + requiredLength > data.length) {
      throw new RangeError(
        `Requested ${requiredLength} byte(s) starting at offset ${offset} exceeds buffer length ${data.length}`
      );
    }
  }

  /** Reads a big-endian 16-bit unsigned integer. */
  static readUint16BE(data: Uint8Array, offset: number = 0): number {
    DataParser.ensureAvailable(data, offset, 2);
    return (data[offset] << 8) | data[offset + 1];
  }

  /** Reads a big-endian 32-bit unsigned integer. */
  static readUint32BE(data: Uint8Array, offset: number = 0): number {
    DataParser.ensureAvailable(data, offset, 4);
    return (
      ((data[offset] << 24) |
        (data[offset + 1] << 16) |
        (data[offset + 2] << 8) |
        data[offset + 3]) >>>
      0
    ); // Ensure unsigned
  }

  /** Reads a big-endian 16-bit signed integer. */
  static readInt16BE(data: Uint8Array, offset: number = 0): number {
    DataParser.ensureAvailable(data, offset, 2);
    const value = (data[offset] << 8) | data[offset + 1];
    return (value << 16) >> 16; // Sign-extend
  }

  /** Reads a big-endian 32-bit signed integer. */
  static readInt32BE(data: Uint8Array, offset: number = 0): number {
    // Reuse the unsigned version and convert to a signed 32-bit integer.
    // Bitwise OR with 0 forces the number into the Int32 range.
    return DataParser.readUint32BE(data, offset) | 0;
  }

  /** Writes a big-endian 16-bit unsigned integer. */
  static writeUint16BE(
    data: Uint8Array,
    value: number,
    offset: number = 0
  ): void {
    DataParser.ensureAvailable(data, offset, 2);
    data[offset] = (value >> 8) & 0xff;
    data[offset + 1] = value & 0xff;
  }

  /** Writes a big-endian 32-bit unsigned integer. */
  static writeUint32BE(
    data: Uint8Array,
    value: number,
    offset: number = 0
  ): void {
    DataParser.ensureAvailable(data, offset, 4);
    data[offset] = (value >> 24) & 0xff;
    data[offset + 1] = (value >> 16) & 0xff;
    data[offset + 2] = (value >> 8) & 0xff;
    data[offset + 3] = value & 0xff;
  }

  /** Reads a null-terminated ASCII string. */
  static readCString(
    data: Uint8Array,
    offset: number = 0,
    maxLength?: number
  ): string {
    DataParser.ensureAvailable(data, offset, 1);
    const end = maxLength
      ? Math.min(offset + maxLength, data.length)
      : data.length;
    let result = '';
    for (let i = offset; i < end; i++) {
      if (data[i] === 0) break;
      result += String.fromCharCode(data[i]);
    }
    return result;
  }

  /** Writes a null-terminated ASCII string. */
  static writeCString(
    data: Uint8Array,
    value: string,
    offset: number = 0,
    maxLength?: number
  ): void {
    DataParser.ensureAvailable(data, offset, 1);
    const available = data.length - offset;
    if (maxLength !== undefined && maxLength < 0) {
      throw new RangeError('CString maxLength must be non-negative');
    }
    const limit =
      maxLength !== undefined ? Math.min(maxLength, available) : available;
    if (limit <= 0) {
      throw new RangeError('CString region must be at least 1 byte to include a terminator');
    }

    const charsToWrite = Math.min(value.length, limit - 1);
    let i = 0;
    for (; i < charsToWrite; i++) {
      data[offset + i] = value.charCodeAt(i);
    }

    data[offset + i] = 0; // Null terminator

    // Clear any remaining space inside the region to avoid leaking stale data
    for (let j = i + 1; j < limit; j++) {
      data[offset + j] = 0;
    }
  }
}
