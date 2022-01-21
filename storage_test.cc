#include "gtest/gtest.h"
#include "storage.h"


static MethodInfo methodInfo1 {.file = "file1", .klass = "klass1", .line = 1, .name = "method1"};
static MethodInfo methodInfo2 {.file = "file2", .klass = "klass2", .line = 2, .name = "method2"};
static AllocationInfo aInfo1 { .ref = 100, .sizeBytes = 24 };
static AllocationInfo aInfo2 { .ref = 101, .sizeBytes = 36 };

TEST(Storage, AddMethod) {

    Storage underTest;
    underTest.AddMethod(1, methodInfo1);
    EXPECT_EQ(underTest.GetMethod(1).name, methodInfo1.name);
    EXPECT_TRUE(underTest.HasMethod(1));
}


TEST(Storage, AddAllocation) {

    Storage underTest;
    const auto methodId = 1;
    const auto stackId = 2;

    underTest.AddMethod(methodId, methodInfo1);

    StackTrace st;
    st.AddFrame(methodId);

    underTest.AddAllocation(stackId, st, aInfo1);

    EXPECT_EQ(underTest.GetStackTrace(stackId).GetFrames()[0], methodId);
}

TEST(Storage, Clear) {

    Storage underTest;
    const auto stackId = 2;

    underTest.AddMethod(1, methodInfo1);
    underTest.AddMethod(2, methodInfo2);

    StackTrace st;
    st.AddFrame(1);
    st.AddFrame(2);
    st.AddFrame(1);

    underTest.AddAllocation(stackId, st, aInfo1);
    underTest.AddAllocation(stackId, st, aInfo2);

    EXPECT_EQ(underTest.allocations.size(), 2);
    EXPECT_EQ(underTest.methods.size(), 2);
    EXPECT_EQ(underTest.GetStackTrace(stackId).GetFrames().size(), 3);


    underTest.Clear();

    EXPECT_EQ(underTest.allocations.size(), 0);
    EXPECT_EQ(underTest.methods.size(), 0);
    EXPECT_EQ(underTest.GetStackTrace(stackId).GetFrames().size(), 0);

}
