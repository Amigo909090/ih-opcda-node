const path = require('path');
const fs = require('fs');

const bindingPath = path.join(__dirname, 'build', 'Release', 'opcda.node');

if (!fs.existsSync(bindingPath)) {
  throw new Error(`Native binding not found at ${bindingPath}. Please rebuild the module.`);
}

module.exports = require(bindingPath);