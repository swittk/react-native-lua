const fs = require('fs');
const path = require('path');
const {getDefaultConfig, mergeConfig} = require('@react-native/metro-config');

const projectRoot = __dirname;
const workspaceRoot = path.resolve(projectRoot, '..');
const exampleNodeModules = path.join(projectRoot, 'node_modules');

function forceExampleRuntime(context, moduleName, platform) {
  if (
    moduleName === 'react' ||
    moduleName.startsWith('react/') ||
    moduleName === 'react-native' ||
    moduleName.startsWith('react-native/')
  ) {
    const filePath = require.resolve(moduleName, {paths: [exampleNodeModules]});
    return {type: 'sourceFile', filePath: fs.realpathSync(filePath)};
  }
  return context.resolveRequest(context, moduleName, platform);
}

const config = {
  watchFolders: [workspaceRoot],
  resolver: {
    unstable_enableSymlinks: true,
    nodeModulesPaths: [exampleNodeModules],
    resolveRequest: forceExampleRuntime,
    extraNodeModules: {
      'react-native-lua': workspaceRoot,
    },
  },
};

module.exports = mergeConfig(getDefaultConfig(projectRoot), config);
