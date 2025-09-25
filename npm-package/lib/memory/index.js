// packages/memory/src/index.ts
var MemoryRegion = class {
  constructor(system, address, size, parser) {
    this.system = system;
    this.address = address;
    this.size = size;
    this.parser = parser;
  }
  /** Reads the memory region and returns the parsed structure. */
  get() {
    const bytes = this.system.readBytes(this.address, this.size);
    return this.parser(bytes);
  }
  /** Writes raw bytes to the memory region. */
  setBytes(data) {
    if (data.length !== this.size) {
      throw new Error(
        `Data size (${data.length}) does not match region size (${this.size})`
      );
    }
    this.system.writeBytes(this.address, data);
  }
};
var MemoryArray = class {
  constructor(system, baseAddress, stride, parser) {
    this.system = system;
    this.baseAddress = baseAddress;
    this.stride = stride;
    this.parser = parser;
  }
  /** Reads and parses the element at the given index. */
  at(index) {
    const address = this.baseAddress + index * this.stride;
    const bytes = this.system.readBytes(address, this.stride);
    return this.parser(bytes);
  }
  /** Writes raw bytes to the element at the given index. */
  setAt(index, data) {
    if (data.length !== this.stride) {
      throw new Error(
        `Data size (${data.length}) does not match stride (${this.stride})`
      );
    }
    const address = this.baseAddress + index * this.stride;
    this.system.writeBytes(address, data);
  }
  /** Creates an iterable over a range of indices. */
  *iterate(start, count) {
    for (let i = start; i < start + count; i++) {
      yield this.at(i);
    }
  }
};
var DataParser = class _DataParser {
  /** Reads a big-endian 16-bit unsigned integer. */
  static readUint16BE(data, offset = 0) {
    return data[offset] << 8 | data[offset + 1];
  }
  /** Reads a big-endian 32-bit unsigned integer. */
  static readUint32BE(data, offset = 0) {
    return (data[offset] << 24 | data[offset + 1] << 16 | data[offset + 2] << 8 | data[offset + 3]) >>> 0;
  }
  /** Reads a big-endian 16-bit signed integer. */
  static readInt16BE(data, offset = 0) {
    const value = data[offset] << 8 | data[offset + 1];
    return value << 16 >> 16;
  }
  /** Reads a big-endian 32-bit signed integer. */
  static readInt32BE(data, offset = 0) {
    return _DataParser.readUint32BE(data, offset) | 0;
  }
  /** Writes a big-endian 16-bit unsigned integer. */
  static writeUint16BE(data, value, offset = 0) {
    data[offset] = value >> 8 & 255;
    data[offset + 1] = value & 255;
  }
  /** Writes a big-endian 32-bit unsigned integer. */
  static writeUint32BE(data, value, offset = 0) {
    data[offset] = value >> 24 & 255;
    data[offset + 1] = value >> 16 & 255;
    data[offset + 2] = value >> 8 & 255;
    data[offset + 3] = value & 255;
  }
  /** Reads a null-terminated ASCII string. */
  static readCString(data, offset = 0, maxLength) {
    const end = maxLength ? Math.min(offset + maxLength, data.length) : data.length;
    let result = "";
    for (let i = offset; i < end; i++) {
      if (data[i] === 0) break;
      result += String.fromCharCode(data[i]);
    }
    return result;
  }
  /** Writes a null-terminated ASCII string. */
  static writeCString(data, value, offset = 0, maxLength) {
    const end = maxLength ? Math.min(offset + maxLength - 1, data.length - 1) : data.length - 1;
    let i = offset;
    for (; i < end && i - offset < value.length; i++) {
      data[i] = value.charCodeAt(i - offset);
    }
    data[i] = 0;
  }
};
export {
  DataParser,
  MemoryArray,
  MemoryRegion
};
