import * as React from 'react';
import { useRef } from 'react';

import { StyleSheet, View, Text, TextInput, Button, KeyboardAvoidingView, Alert, ScrollView, Platform } from 'react-native';
import { LuaInterpreter, luaInterpreter, LUA_ERROR_CODE, multiply } from 'react-native-lua';

const defaultInterpString = `co = coroutine.create(function ()
for i=1,10 
do
  print("co", i)
  print(i * 2)
  coroutine.yield()
end
end)

i = 0
while(i < 10)
do
  coroutine.resume(co)
  sleep(1000)
  i = i + 1
end
`
const defaultAndroidString = `i = 0
while(i < 10) 
do print(i) i = i + 1
end`

function useAnimationFrameCallback(cb: (dt: number) => void, deps: any[]) {
  const frame = useRef<ReturnType<typeof requestAnimationFrame>>();
  const prev = useRef(Date.now());
  const animate = () => {
    const now = Date.now();
    // In seconds ~> you can do ms or anything in userland
    cb(now - prev.current);
    prev.current = now;
    frame.current = requestAnimationFrame(animate);
  };

  React.useEffect(() => {
    frame.current = requestAnimationFrame(animate);
    return () => cancelAnimationFrame(frame.current!);
  }, [cb, ...deps]);
};

export default function App() {
  const [result, setResult] = React.useState<number | undefined>();
  const [interpText, setInterpText] = React.useState<string | undefined>(
    // Platform.OS == 'ios' ? defaultInterpString : defaultAndroidString
    defaultInterpString
  );
  const [outputText, setOutputText] = React.useState<string>();
  const tInputRef = React.useRef<TextInput>(null);
  React.useEffect(() => {
    multiply(3, 7).then(setResult);
  }, []);
  const __interpreter = useRef<LuaInterpreter>();
  const getInterpreter = React.useCallback(() => {
    if (!__interpreter.current) {
      __interpreter.current = luaInterpreter();
    }
    return __interpreter.current;
  }, []);
  const refreshOutputText = React.useCallback(() => {
    const interpreter = getInterpreter();
    if (interpreter.printCount) {
      setOutputText((outputText ? outputText + '\n' : '') + interpreter.getPrint());
    }
  }, [outputText]);
  useAnimationFrameCallback(React.useCallback((_dt) => {
    const interpreter = getInterpreter();
    if (interpreter.printCount) {
      refreshOutputText();
    }
  }, [refreshOutputText]), []);
  // React.useEffect(() => {
  //   const interp = interpreter.current;
  //   //     let result = interp.dostring(
  //   interp.getglobal('co');
  //   const a = interp.tothread(-1);
  //   console.log('got a', a);
  //   console.log('type val', interp.type(-1));
  //   console.log('uppermost type', interp.typename(interp.type(-1)));
  //   console.log('print count', interp.printCount);
  //   console.log('print out', interp.getPrint());
  // })

  return (
    <View style={styles.container}>
      <View style={{ height: 20 }} />
      <Button title='Reload New Interpreter' onPress={() => {
        __interpreter.current = luaInterpreter();
      }} />
      <Button title='Dismiss Keyboard' onPress={() => {
        tInputRef.current?.blur();
      }} />
      <Button title='Execute' onPress={() => {
        if (!interpText) return;
        // let result = interpreter.current.dostring(interpText);
        // console.log('exec result', result);
        // if (result != 0) {
        //   const errStr = interpreter.current.getLatestError();
        //   console.log('exec error', errStr);
        //   Alert.alert('Error', errStr);
        // }
        const interpreter = getInterpreter();
        // if (Platform.OS == 'ios') {
        // if (true) {
          interpreter.dostringasync(interpText, (result) => {
            console.log('exec result', result);
            if (result == LUA_ERROR_CODE.LUA_CRASHED_INTERPRETER) {
              Alert.alert('Error', "Interpreter has crashed");
              return;
            }
            if (result != 0) {
              const errStr = interpreter.getLatestError();
              console.log('exec error', errStr);
              Alert.alert('Error', errStr);
            }
          });
        // }
        // else {
        //   let result = interpreter.dostring(interpText);
        //   console.log('exec result', result);
        //   if (result != 0) {
        //     const errStr = interpreter.getLatestError();
        //     console.log('exec error', errStr);
        //     Alert.alert('Error', errStr);
        //   }
        // }
        setInterpText(undefined);
      }} />
      <KeyboardAvoidingView style={{ flex: 1 }}>
        <TextInput
          ref={tInputRef}
          style={styles.textInput}
          textAlignVertical='top'
          multiline={true}
          autoCapitalize='none'
          autoCompleteType='off'
          autoCorrect={false}
          onChangeText={setInterpText}
          value={interpText}
          keyboardType='ascii-capable'
        />
      </KeyboardAvoidingView>
      <ScrollView style={{ flex: 1, backgroundColor: 'black' }}>
        <Text style={{ color: 'green' }}>
          {outputText}
        </Text>
      </ScrollView>
      <Button
        title='Refresh Output'
        onPress={refreshOutputText}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    justifyContent: 'center',
  },
  box: {
    width: 60,
    height: 60,
    marginVertical: 20,
  },
  textInput: {
    flex: 1, margin: 8,
    borderWidth: 1, borderColor: '#888',
    borderRadius: 8,
  }
});
