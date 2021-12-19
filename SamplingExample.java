import java.util.Stack;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.nio.file.*;

class SamplingExample {

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
        if (SamplingExample.c.getAndIncrement() % 2 == 0) {
            SamplingExample.stk.push(a);
        }
        return a;
    }

    private static A warmup() {
        var n = 8*1024*10;
        for (int i = 0; i < n; i++) {
            SamplingExample.stk.push(new A());
        }
        for (int i = 0; i < n - 1; i++) {
            SamplingExample.stk.pop();
        }
        return SamplingExample.stk.pop();
    }

    public static void main(String[] args) throws Exception {
        var a = new AtomicReference<A>();
        var exec = Executors.newFixedThreadPool(2);
        exec.submit(() -> {}).get();

        // Start sampling in the background
        Future<byte[]> profileResult = Heapz.sampleFor(5);

        a.set(SamplingExample.warmup());

        Runnable task = () -> {
            for (int i = 0; i < 8*1024*12; i++) {
                a.set(SamplingExample.culprit());
            }
        };

        exec.execute(task);
        // exec.execute(task);

        // Wait until sample is collected
        byte[] profile = profileResult.get();
        Path path = Paths.get("sample.prof");
        Files.write(path, profile);
        exec.shutdown();
        Heapz.shutdown();
    }
}
