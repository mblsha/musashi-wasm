// post.js
// CommonJS-compatible export for Node.js
if (typeof module !== 'undefined' && module.exports) {
    module.exports.getModule = function() {
        return Module;
    };
}
