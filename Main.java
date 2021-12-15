import java.util.Stack;
import java.util.concurrent.atomic.AtomicInteger;

class Main {

    private static Stack<A> stk = new Stack<>();
    private static AtomicInteger c = new AtomicInteger();

    // 16 + 14*8 = 128
    static class A {
        private double d1;
        private double d2;
        private double d3;
        private double d4;
        private double d5;
        private double d6;
        private double d7;
        private double d8;
        private double d9;
        private double d10;
        private double d11;
        private double d12;
        private double d13;
        private double d14;
    }


    private static A culprit() {
        A a = new A();
        if (Main.c.getAndIncrement() % 2 == 0) {
            Main.stk.push(a);
        }
        return a;
    }

    private static A warmup() {
        var n = 8*1024*10;
        for (int i = 0; i < n; i++) {
            Main.stk.push(new A());
        }
        for (int i = 0; i < n - 1; i++) {
            Main.stk.pop();
        }
        return Main.stk.pop();
    }

    public static void main(String[] args) throws Exception {
        Thread.sleep(100);
        A a = Main.warmup();
        System.gc();
        Thread.sleep(100);
        a.d13 = a.d11 + a.d12;
        System.gc();
        for (int i = 0; i < 8*1024*15; i++) {
            a = Main.culprit();
            if (Main.c.get() % 1_000_000 == 0) {
                System.gc();
            }
        }
    }
}
