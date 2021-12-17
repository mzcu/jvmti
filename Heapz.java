public class Heapz {

    public static native void startSampling();
    public static native void stopSampling();
    public static native byte[] getResults();


    public static byte[] sampleFor(int seconds) throws Exception {
        Heapz.startSampling();
        Thread.sleep(seconds*1000); // TODO: timer task
        Heapz.stopSampling();
        return Heapz.getResults();
    }

}
