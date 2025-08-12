import { MemoryRegion, MemoryArray, DataParser } from './index';
import type { System } from '@m68k/core';

// Mock System for testing
class MockSystem implements System {
  private memory = new Map<number, number>();
  
  read(address: number, size: 1 | 2 | 4): number {
    let value = 0;
    for (let i = 0; i < size; i++) {
      value = (value << 8) | (this.memory.get(address + i) || 0);
    }
    return value;
  }
  
  write(address: number, size: 1 | 2 | 4, value: number): void {
    for (let i = size - 1; i >= 0; i--) {
      this.memory.set(address + i, (value >> (8 * (size - 1 - i))) & 0xFF);
    }
  }
  
  readBytes(address: number, length: number): Uint8Array {
    const result = new Uint8Array(length);
    for (let i = 0; i < length; i++) {
      result[i] = this.memory.get(address + i) || 0;
    }
    return result;
  }
  
  writeBytes(address: number, data: Uint8Array): void {
    for (let i = 0; i < data.length; i++) {
      this.memory.set(address + i, data[i]);
    }
  }
  
  // Stub implementations for other System methods
  getRegisters(): any { return {}; }
  setRegister(): void {}
  async call(): Promise<number> { return 0; }
  async run(): Promise<number> { return 0; }
  reset(): void {}
  probe(): () => void { return () => {}; }
  override(): () => void { return () => {}; }
  tracer: any = {
    isAvailable: () => false,
    start: () => {},
    stop: async () => new Uint8Array(0),
    registerFunctionNames: () => {},
    registerMemoryNames: () => {}
  };
}

describe('@m68k/memory', () => {
  let system: MockSystem;
  
  beforeEach(() => {
    system = new MockSystem();
  });
  
  describe('MemoryRegion', () => {
    interface TestStruct {
      magic: number;
      version: number;
      flags: number;
    }
    
    const parser: (data: Uint8Array) => TestStruct = (data) => ({
      magic: DataParser.readUint32BE(data, 0),
      version: DataParser.readUint16BE(data, 4),
      flags: DataParser.readUint16BE(data, 6)
    });
    
    test('should read a memory region', () => {
      const address = 0x1000;
      const size = 8;
      
      // Write test data
      system.write(address, 4, 0xDEADBEEF);
      system.write(address + 4, 2, 0x0102);
      system.write(address + 6, 2, 0x0304);
      
      const region = new MemoryRegion(system, address, size, parser);
      const data = region.get();
      
      expect(data.magic).toBe(0xDEADBEEF);
      expect(data.version).toBe(0x0102);
      expect(data.flags).toBe(0x0304);
    });
    
    test('should write bytes to a memory region', () => {
      const address = 0x2000;
      const size = 8;
      const region = new MemoryRegion(system, address, size, parser);
      
      const testData = new Uint8Array([
        0x12, 0x34, 0x56, 0x78, // magic
        0xAB, 0xCD,             // version
        0xEF, 0x01              // flags
      ]);
      
      region.setBytes(testData);
      
      expect(system.read(address, 4)).toBe(0x12345678);
      expect(system.read(address + 4, 2)).toBe(0xABCD);
      expect(system.read(address + 6, 2)).toBe(0xEF01);
    });
    
    test('should throw on size mismatch', () => {
      const region = new MemoryRegion(system, 0x3000, 8, parser);
      const wrongSizeData = new Uint8Array(10);
      
      expect(() => region.setBytes(wrongSizeData)).toThrow('Data size (10) does not match region size (8)');
    });
  });
  
  describe('MemoryArray', () => {
    interface Entity {
      x: number;
      y: number;
      health: number;
    }
    
    const entityParser: (data: Uint8Array) => Entity = (data) => ({
      x: DataParser.readInt16BE(data, 0),
      y: DataParser.readInt16BE(data, 2),
      health: DataParser.readUint16BE(data, 4)
    });
    
    test('should read array elements', () => {
      const baseAddress = 0x4000;
      const stride = 6; // 3 x 16-bit values
      const array = new MemoryArray(system, baseAddress, stride, entityParser);
      
      // Write test entities
      system.write(baseAddress, 2, 100);      // entity[0].x
      system.write(baseAddress + 2, 2, 200);  // entity[0].y
      system.write(baseAddress + 4, 2, 255);  // entity[0].health
      
      system.write(baseAddress + 6, 2, -50);  // entity[1].x (signed)
      system.write(baseAddress + 8, 2, 300);  // entity[1].y
      system.write(baseAddress + 10, 2, 128); // entity[1].health
      
      const entity0 = array.at(0);
      expect(entity0.x).toBe(100);
      expect(entity0.y).toBe(200);
      expect(entity0.health).toBe(255);
      
      const entity1 = array.at(1);
      expect(entity1.x).toBe(-50);
      expect(entity1.y).toBe(300);
      expect(entity1.health).toBe(128);
    });
    
    test('should write array elements', () => {
      const baseAddress = 0x5000;
      const stride = 6;
      const array = new MemoryArray(system, baseAddress, stride, entityParser);
      
      const entityData = new Uint8Array(6);
      DataParser.writeUint16BE(entityData, 42, 0);   // x
      DataParser.writeUint16BE(entityData, 84, 2);   // y
      DataParser.writeUint16BE(entityData, 100, 4);  // health
      
      array.setAt(2, entityData); // Write to index 2
      
      const address = baseAddress + 2 * stride;
      expect(system.read(address, 2)).toBe(42);
      expect(system.read(address + 2, 2)).toBe(84);
      expect(system.read(address + 4, 2)).toBe(100);
    });
    
    test('should iterate over array elements', () => {
      const baseAddress = 0x6000;
      const stride = 4;
      const array = new MemoryArray(system, baseAddress, stride, (data) => 
        DataParser.readUint32BE(data, 0)
      );
      
      // Write test values
      for (let i = 0; i < 5; i++) {
        system.write(baseAddress + i * stride, 4, i * 100);
      }
      
      const values: number[] = [];
      for (const value of array.iterate(1, 3)) { // Start at 1, count 3
        values.push(value);
      }
      
      expect(values).toEqual([100, 200, 300]);
    });
  });
  
  describe('DataParser', () => {
    test('should read unsigned integers', () => {
      const data = new Uint8Array([0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD]);
      
      expect(DataParser.readUint16BE(data, 0)).toBe(0x1234);
      expect(DataParser.readUint16BE(data, 2)).toBe(0x5678);
      expect(DataParser.readUint32BE(data, 0)).toBe(0x12345678);
      expect(DataParser.readUint32BE(data, 2)).toBe(0x5678ABCD);
    });
    
    test('should read signed integers', () => {
      const data = new Uint8Array([0xFF, 0xFE, 0x80, 0x00, 0x7F, 0xFF]);
      
      expect(DataParser.readInt16BE(data, 0)).toBe(-2);
      expect(DataParser.readInt16BE(data, 2)).toBe(-32768);
      expect(DataParser.readInt16BE(data, 4)).toBe(32767);
    });
    
    test('should write unsigned integers', () => {
      const data = new Uint8Array(6);
      
      DataParser.writeUint16BE(data, 0x1234, 0);
      DataParser.writeUint32BE(data, 0xABCDEF01, 2);
      
      expect(data).toEqual(new Uint8Array([0x12, 0x34, 0xAB, 0xCD, 0xEF, 0x01]));
    });
    
    test('should read C strings', () => {
      const data = new Uint8Array([
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x00, // "Hello\0"
        0x57, 0x6F, 0x72, 0x6C, 0x64        // "World" (no terminator)
      ]);
      
      expect(DataParser.readCString(data, 0)).toBe('Hello');
      expect(DataParser.readCString(data, 6, 5)).toBe('World');
      expect(DataParser.readCString(data, 0, 3)).toBe('Hel');
    });
    
    test('should write C strings', () => {
      const data = new Uint8Array(10);
      
      DataParser.writeCString(data, 'Test', 0);
      expect(data.slice(0, 5)).toEqual(new Uint8Array([0x54, 0x65, 0x73, 0x74, 0x00]));
      
      DataParser.writeCString(data, 'LongString', 5, 4);
      expect(data.slice(5, 9)).toEqual(new Uint8Array([0x4C, 0x6F, 0x6E, 0x00]));
    });
  });
});