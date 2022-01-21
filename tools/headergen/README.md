# `rellic-headergen`

## What is it?
This utility generates C code that describes the layout of data structures as defined by the DWARF metadata contained inside of LLVM modules.

## Why would I use it?
`rellic-headergen` is useful when needing to "flatten" the layout of C++ code for example, which can contain classes and complex inheritance relations. The output is valid C code that matches the memory layout of the original C++.

## How do I use it?
After compiling `rellic`, you can use `rellic-headergen` like so:
```sh
$ rellic-headergen --input path/to/module.bc --output layout.c
```

As an example, compiling the following code
```c++
class Vehicle {
 public:
  virtual ~Vehicle(void);
  virtual void Drive(float speed) = 0;
};

enum Color {
  kRed,
  kBlack,
  kWhite,
  kBlue
};

class Car : public Vehicle {
 private:
  Color color;
  float mileage;

 public:
  explicit Car(Color color_);

  virtual ~Car(void);
  void Drive(float speed) override;
};

void force(Car *c) {
  c->Drive(10.0);
}
```

using clang and then running `rellic-headergen`
```sh
$ clang++ -std=c++17 -O0 -gfull -emit-llvm -c test.cpp -o test.bc
$ rellic-headergen --input test.bc --output /dev/stderr
```

produces the following C code
```c
struct Car_0;
enum Color_1 {
    kRed_0 = 0UL,
    kBlack_1 = 1UL,
    kWhite_2 = 2UL,
    kBlue_3 = 3UL
};
struct Vehicle_2;
struct Car_0 {
    int (**_vptr_Vehicle0)(void);
    enum Color_1 color1;
    float mileage2;
};
struct Vehicle_2 {
    int (**_vptr_Vehicle0)(void);
};
void _Z5forceP3Car(struct Car_0 *arg1);
```