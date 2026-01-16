// Helper module that imports another module
import { add } from './test_module.js';

export function multiply(a, b) {
    return a * b;
}

export function addAndMultiply(a, b, c) {
    return add(a, b) * c;
}
