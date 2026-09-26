const path = require('path');
const {getDefaultConfig, mergeConfig} = require('@react-native/metro-config');

const config = {
  watchFolders: [path.resolve(__dirname, '..')],
  resolver: {
    // The package source is outside example/, but React and RN must resolve to
    // this RN 0.73.6 app so Metro never loads the root RN 0.83 dev dependency.
    extraNodeModules: {
      react: path.join(__dirname, 'node_modules/react'),
      'react-native': path.join(__dirname, 'node_modules/react-native'),
    },
  },
};

module.exports = mergeConfig(getDefaultConfig(__dirname), config);
