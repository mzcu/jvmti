import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

public class Heapz {

    private static final ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
    private static final ReentrantLock samplingInProgress = new ReentrantLock();

    public static void shutdown() {
        executor.shutdown();
    }

    public static Future<byte[]> sampleFor(int seconds) {
        Heapz.startSampling();
        return executor.schedule(() -> {
            var locked = false;
            try {
                Heapz.stopSampling();
                if (!(locked = samplingInProgress.tryLock())) {
                    throw new IllegalStateException("Sampling already in progress");
                }
                return Heapz.getResults();
            } finally {
                if (locked) samplingInProgress.unlock();
            }
        }, seconds, TimeUnit.SECONDS);
    }

    // Implemented in heapz.cc
    public static native void startSampling();

    public static native void stopSampling();

    public static native byte[] getResults();

}
