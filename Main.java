import java.util.List;
import java.util.LinkedList;
import java.util.concurrent.atomic.AtomicReference;

class Main {
    static class A {
        private final double d1 = 1d;
        private final double d2 = 100d;
    }
    static class B extends A {}
    private static List<A> other() {
        var as = new LinkedList<A>();
        for (int i = 0; i < 10_000; i++) {
            as.add(new A());
        }
        return as;
    }

    public static void main(String[] args) throws Exception {
        var ref = new AtomicReference<A>();
        Thread t1 = new Thread(() -> {
                List<A> as = Main.other();
        });
        Thread t2 = new Thread(() -> {
                List<A> as = Main.other();
                ref.set(as.get(0));
        });
        t1.start(); t2.start();
        t1.join(); t2.join();
        System.gc();
    }
}
