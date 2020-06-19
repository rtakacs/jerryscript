// Copyright JS Foundation and other contributors, http://js.foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

object = {};

var descriptor1 = {
  value: 42,
  writable: true,
  enumerable: true,
  configurable: true
}

var descriptor2 = {
  value: "foo",
  writable: true,
  enumerable: false,
  configurable: true
}

var descriptor3 = {
  value: undefined,
  writable: false,
  enumerable: true,
  configurable: false
}

Object.defineProperties(object, {
  "property1": descriptor1,
  "property2": descriptor2,
  "property3": descriptor3
});

var object_desc = Object.getOwnPropertyDescriptors(object);

assert(JSON.stringify(Object.getOwnPropertyNames(object_desc)) === '["property1","property2","property3"]');

assert(object_desc.property1.value === 42);
assert(object_desc.property1.writable === true);
assert(object_desc.property1.enumerable === true);
assert(object_desc.property1.configurable === true);

assert(object_desc.property2.value === "foo");
assert(object_desc.property2.writable === true);
assert(object_desc.property2.enumerable === false);
assert(object_desc.property2.configurable === true);

assert(object_desc.property3.value === undefined);
assert(object_desc.property3.writable === false);
assert(object_desc.property3.enumerable === true);
assert(object_desc.property3.configurable === false);

var array_desc = Object.getOwnPropertyDescriptors(Array);

assert (array_desc.prototype.value === Array.prototype);
assert (array_desc.prototype.writable === false);
assert (array_desc.prototype.configurable === false);
assert (array_desc.prototype.enumerable === false);

try {
    Object.getOwnPropertyDescriptors (undefined);
    assert (false);
} catch (e) {
    assert (e instanceof TypeError);
}

