"use strict";
test: {
    try {
        try {
            break test;
        } finally {
            print(1)
        }
    } finally {
        print(2)
    }
}
print(3)