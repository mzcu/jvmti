public class Heapz {
    public static native void startSampling();
    public static native void stopSampling();
    public static native byte[] getResults();


    public static void sampleFor(int seconds) throws Exception {
        Heapz.startSampling();
        Thread.sleep(seconds*1000); // TODO: timer task
        Heapz.stopSampling();
        System.out.println(Heapz.getResults().length);
    }

    public static void main(String[] args) throws Exception {
        Heapz.sampleFor(5);
    }
}
