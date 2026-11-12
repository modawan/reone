# Code Style

## General

Add a copyright notice to the beginning of each file.

In header files, use `#pragma once` instead of include guards.

In header files, when another class is used only by reference or a raw pointer, forward declare it.

Keep function definitions in header files, unless a separate translation unit is required or is practical.

Must avoid `using namespace` in header files, use it only for `reone` namespaces in source files.

Arrange class members in the following order: constants, enums, classes/structs, variables, constructors/destructors, methods, getters, setters.

When passing a variable as an argument to a function or returning a variable from a function:

- Always pass and return primitive type variables, e.g. `int`, by value
- When a function argument is to be used in a computation and never stored, pass it by reference, preferably constant
- When a function argument is to be stored, pass it by value or a smart pointer
- Never pass or return a raw pointer

When initializing a variable, use brace syntax for direct construction, and assignment syntax for copy and move construction. 

## Naming

Use Pascal case for class names, e.g. `FooBar`.

Use Camel case for function names and struct members, e.g. `fooBar`.

Use Camel case with a leading underscore for class member variables, e.g. `_fooBar`.

Prefix constants with a letter "k", e.g. `kFooBar`.

Prefix global variables with a letter "g", e.g. `g_fooBar`.

## Formatting

Use clang-format to auto-format files on save.

## Comments

Use Javadoc-style comments for class, function and variable declarations.

```cpp
/**
 * Calculates the square of a number.
 *
 * @param x number to calculate the square of 
 * @return square of a number
 */
int square(int x);
```

Use C++-style comments for code folding and implementation notes.

```cpp
// Test methods

void should_square_a_number() {
    // given
    int x = 2;

    // when
    int xx = square(x);

    // then
    assert(x == 4);
}

// END Test methods
```

## Includes

Group and arrange includes in the following order:

1. Companion header file if source file
1. System libraries
1. Third-party libraries
1. Project libraries
