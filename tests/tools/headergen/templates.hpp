template <typename A>
class W {
 public:
  template <typename B>
  static int X(void);


  template <typename B, typename C>
  class Y {
   public:

    template <typename D>
    static int Z(void);
  };

  template <typename B>
  class Y<B, B> {
   public:

    template <typename D>
    static int Z(void);
  };

  class Evil {
   public:
    static int Monster(void);
  };
};

template <typename A>
template <typename B>
class W<A>::Y<float, B> {
 public:

  template <typename D>
  static int Z(void);
};

template <typename A>
template <typename B>
int W<A>::X(void) {
  return 1;
}

template <typename A>
template <typename B, typename C>
template <typename D>
int W<A>::Y<B, C>::Z(void) {
  return 1;
}

template <typename A>
int W<A>::Evil::Monster(void) {
  return 1;
}

int main(void) {
  return W<int>::X<int>() + W<int>::Y<int, int>::Z<int>() +
         W<int>::Y<float, int>::Z<int>() +
         W<int>::Y<int, float>::Z<int>() +
         W<int>::Evil::Monster() +
         W<float>::Evil::Monster();
}