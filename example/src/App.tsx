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
    let result = interp.dostring(
`co = coroutine.create(function ()
for i=1,10 do
  print("co", i)
  coroutine.yield()
end
end)
coroutine.resume(co)
`);
    interp.getglobal('co');
    const a = interp.tothread(-1);
    console.log('got a', a);
    console.log('type val', interp.type(-1));
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
