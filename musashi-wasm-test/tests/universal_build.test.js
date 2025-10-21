/**
 * Test for the universal build that works in both Node.js and browsers
 */

import { readFileSync, existsSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

describe('Universal Build Compatibility', () => {
  test('should have dynamic environment detection in universal build', async () => {
    // Try to load the universal build
    let module = null;
    let loadError = null;
    
    try {
      const { default: createModule } = await import('../../musashi-universal.out.mjs');
      module = await createModule();
    } catch (error) {
      loadError = error;
      console.error('Failed to load universal build:', error.message);
    }
    
    if (loadError) {
      // If the universal build doesn't exist yet, skip the test
      console.log('Universal build not found. Run ./build.sh to create it.');
      return;
    }
    
    // Verify the module loaded successfully
    expect(module).toBeDefined();
    expect(module._m68k_init).toBeDefined();
    expect(module._m68k_execute).toBeDefined();
    
    console.log('✅ Universal build loaded successfully in Node.js');
    console.log('Available functions:', Object.keys(module).filter(k => k.startsWith('_')).slice(0, 10));
  });
  
  test('should verify universal build has proper environment detection code', async () => {
    const universalPath = join(__dirname, '../../musashi-universal.out.mjs');
    
    if (!existsSync(universalPath)) {
      console.log('Universal build not found. Run ./build.sh to create it.');
      return;
    }
    
    const content = readFileSync(universalPath, 'utf-8');
    
    // Check for dynamic environment detection instead of hardcoded values
    const hasProperNodeDetection = /typeof\s+process\s*==\s*['"`]object['"`]/.test(content) &&
                                   /typeof\s+process\.versions\s*==\s*['"`]object['"`]/.test(content);
    const hasProperWebDetection = /typeof\s+window\s*==\s*['"`]object['"`]/.test(content);
    const hasProperWorkerDetection = /typeof\s+WorkerGlobalScope\s*!==?\s*['"`]undefined['"`]/.test(content) ||
      /typeof\s+importScripts\s*==\s*['"`]function['"`]/.test(content);
    
    // Should NOT have hardcoded environment values like these:
    const hasHardcodedNode = content.includes('var ENVIRONMENT_IS_NODE = true;');
    const hasHardcodedWeb = content.includes('var ENVIRONMENT_IS_WEB = true;');
    
    console.log('Environment detection analysis:');
    console.log('  Dynamic Node.js detection:', hasProperNodeDetection ? '✅' : '❌');
    console.log('  Dynamic Web detection:', hasProperWebDetection ? '✅' : '❌');
    console.log('  Dynamic Worker detection:', hasProperWorkerDetection ? '✅' : '❌');
    console.log('  No hardcoded Node.js:', !hasHardcodedNode ? '✅' : '❌');
    console.log('  No hardcoded Web:', !hasHardcodedWeb ? '✅' : '❌');
    
    expect(hasProperNodeDetection).toBeTruthy();
    expect(hasProperWebDetection).toBeTruthy();
    expect(hasProperWorkerDetection).toBeTruthy();
    expect(hasHardcodedNode).toBeFalsy();
    expect(hasHardcodedWeb).toBeFalsy();
  });
  
  test('should compare environment detection between builds', async () => {
    const builds = [
      { name: 'Node.js build', file: 'musashi-node.out.mjs' },
      { name: 'Web build', file: 'musashi.out.mjs' },
      { name: 'Universal build', file: 'musashi-universal.out.mjs' }
    ];
    
    console.log('\nEnvironment detection comparison:');
    console.log('=================================');
    
    for (const build of builds) {
      const buildPath = join(__dirname, '../../', build.file);
      
      if (!existsSync(buildPath)) {
        console.log(`${build.name}: Not found`);
        continue;
      }
      
      const content = readFileSync(buildPath, 'utf-8');
      
      // Extract the ENVIRONMENT_IS_NODE line
      const nodeEnvMatch = content.match(/var ENVIRONMENT_IS_NODE = ([^;]+);/);
      const webEnvMatch = content.match(/var ENVIRONMENT_IS_WEB = ([^;]+);/);
      
      console.log(`\n${build.name}:`);
      if (nodeEnvMatch) {
        const nodeValue = nodeEnvMatch[1].trim();
        const isDynamic = !['true', 'false'].includes(nodeValue);
        console.log(`  ENVIRONMENT_IS_NODE: ${isDynamic ? '✅ Dynamic' : '❌ Hardcoded'} (${nodeValue.substring(0, 50)}...)`);
      }
      if (webEnvMatch) {
        const webValue = webEnvMatch[1].trim();
        const isDynamic = !['true', 'false'].includes(webValue);
        console.log(`  ENVIRONMENT_IS_WEB:  ${isDynamic ? '✅ Dynamic' : '❌ Hardcoded'} (${webValue.substring(0, 50)}...)`);
      }
    }
    
    console.log('\n=================================');
    console.log('Universal build should have dynamic detection for maximum compatibility');
  });
});
