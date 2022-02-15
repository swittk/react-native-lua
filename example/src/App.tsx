import * as React from 'react';

import { StyleSheet, View, Text } from 'react-native';
import { luaInterpreter, multiply } from 'react-native-lua';

export default function App() {
  const [result, setResult] = React.useState<number | undefined>();

  React.useEffect(() => {
    multiply(3, 7).then(setResult);
  }, []);
  React.useEffect(() => {
    const interp = luaInterpreter();
    interp.dostring(`a = 2
b = a ^ 2
a = b * 20
    `);
    interp.getglobal('a');
    const a = interp.tointeger(-1);
    console.log('got a', a);
    console.log('uppermost type', interp.typename(interp.type(-1)));
  })

  return (
    <View style={styles.container}>
      <Text>Result: {result}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
  },
  box: {
    width: 60,
    height: 60,
    marginVertical: 20,
  },
});
