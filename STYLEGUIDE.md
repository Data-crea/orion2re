# Project code style guide

New code follows the conventions already established by the restored game source. Configuration runtime code, generated code, build tools, and tests follow the same conventions unless this document states a narrow exception.

## General style

- Write C-oriented C++20: plain structs, free functions, explicit state, namespaces, and straightforward control flow.
- Prefer data and operations with clear ownership over class hierarchies, fluent interfaces, metaprogramming, and framework-style abstractions.
- Keep implementation details in `.cpp` files. Public headers expose only the declarations required by their consumers.
- Use fixed-width integer types when size matters.
- Pass optional or mutable objects by pointer. Use `const` pointers for read-only input. Use references only where existing APIs or required standard-library interfaces make them clearer.
- Return status explicitly and write detailed results or diagnostics through output parameters when that matches surrounding code.
- Do not add exceptions or RTTI-dependent designs to project-owned code. Report parse, validation, generation, and I/O failures explicitly.
- Comments explain constraints or non-obvious reasoning; they do not narrate the code.

## Names

- Functions use `Pascal_Case_With_Underscores`, for example `Parse_Config_File` and `Generate_Config_Bindings`.
- Struct types use the existing `s_lower_case_with_underscores` form.
- Enum types use the existing `e_lower_case_with_underscores` form.
- Type aliases use the existing `t_lower_case_with_underscores` form.
- Local variables, parameters, data members, and namespace-scope data use `lower_case_with_underscores`.
- Constants and enum values follow the convention of the owning subsystem; new configuration code should use an uppercase subsystem prefix when values enter a shared namespace.
- Namespaces use the owning subsystem name. Configuration code uses `config`; build-tool-only code uses `config::tool`.
- Source filenames use `lower_case_with_underscores.cpp` and `.h`.
- Avoid abbreviations unless they are established game or protocol terms.

## C-with-namespaces organization

- Group subsystem declarations in a namespace instead of creating a static utility class or singleton class.
- Represent state with plain `s_` structs and operate on it with free functions.
- Prefer explicit initialization and shutdown functions when state owns resources.
- Keep native configuration state owned by the relevant gameplay subsystem. Generated bindings reference that state; they do not create an unrelated global object model.
- Avoid inheritance and virtual dispatch unless an existing subsystem requires them.
- Avoid operator overloading, implicit conversion APIs, and hidden global registration.

## Templates and standard-library types

- Prefer concrete structs and functions over templates when they keep the implementation clear.
- Avoid template metaprogramming, concepts, and template-heavy frameworks in project-owned public APIs.
- Use standard-library containers according to their semantics. Do not depend on iteration order from unordered containers.
- Prefer direct ownership and explicit lifetimes. Use `std::unique_ptr` when a value cannot be stored directly and single ownership is required.
- Keep third-party implementation types out of project-owned public APIs where practical.

## Generated code

- Generated C++ follows this style guide and should resemble maintained project code.
- Generated files contain a short generated-file notice and identify their canonical source.
- Generated files use deterministic order and formatting.
- Generated files are never edited by hand.
- Generated APIs expose concrete structs and functions, not templates.
- Native builds with configuration tooling available regenerate checked-in output when the canonical schema or generator changes. Cross builds and builds without the tool dependency compile the checked-in output directly.

## Dependency policy

Before adding a C or C++ dependency, verify its license, maintenance status, supported targets, diagnostics, security limits, and transitive dependencies. Prefer a small source dependency with a C-compatible boundary over a header-only C++ library that exports template-heavy types. Vendor accepted dependencies consistently with the repository's existing dependency policy and pin an exact revision.

Do not add a dependency when a small purpose-built component is required anyway to preserve source locations, declaration order, deterministic output, or project-specific diagnostics.

## Tests

- Use the repository's existing doctest setup for C++ unit tests.
- Test public behavior and diagnostics rather than private implementation details.
- Keep tests for distinct processing phases separate so failures identify the responsible phase.
- Include limits and malformed-input cases for parser entry points.