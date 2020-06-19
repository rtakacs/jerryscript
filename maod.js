const object1 = {
  a: 'somestring',
  b: 42,
  [Symbol('secret')]: 'I am scared!',
};

function getFunc() {
    throw 2;
}


Object.defineProperty(object1, 'foo', { enumerable: true, get: getFunc});

for (let [a, b] of Object.entries(object1)) {
  print("[ " + a + ", " + b + " ]");
}
