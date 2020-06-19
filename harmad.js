const object = {
  a: 111,
  [Symbol('secret')]: 'foo',
  b: 4
};

const desc =  {value: 3, enumerable: false, writable: true, configurable: false};
Object.defineProperty(monster1, 'foo', desc);

const handler1 = {
  ownKeys(target) {
    return Reflect.ownKeys(target);
  }
};

const proxy1 = new Proxy(monster1, handler1);

for (let key of Object.keys(proxy1)) {
  console.log(key);
}
