; ModuleID = '/sn640/NotDec/test/backend/../wasm/sysy/functional/32_while_if_test2.c'
source_filename = "/sn640/NotDec/test/backend/../wasm/sysy/functional/32_while_if_test2.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @ifWhile() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  store i32 3, i32* %2, align 4
  %3 = load i32, i32* %1, align 4
  %4 = icmp eq i32 %3, 5
  br i1 %4, label %5, label %15

5:                                                ; preds = %0
  br label %6

6:                                                ; preds = %9, %5
  %7 = load i32, i32* %2, align 4
  %8 = icmp eq i32 %7, 2
  br i1 %8, label %9, label %12

9:                                                ; preds = %6
  %10 = load i32, i32* %2, align 4
  %11 = add nsw i32 %10, 2
  store i32 %11, i32* %2, align 4
  br label %6, !llvm.loop !6

12:                                               ; preds = %6
  %13 = load i32, i32* %2, align 4
  %14 = add nsw i32 %13, 25
  store i32 %14, i32* %2, align 4
  br label %25

15:                                               ; preds = %0
  br label %16

16:                                               ; preds = %19, %15
  %17 = load i32, i32* %1, align 4
  %18 = icmp slt i32 %17, 5
  br i1 %18, label %19, label %24

19:                                               ; preds = %16
  %20 = load i32, i32* %2, align 4
  %21 = mul nsw i32 %20, 2
  store i32 %21, i32* %2, align 4
  %22 = load i32, i32* %1, align 4
  %23 = add nsw i32 %22, 1
  store i32 %23, i32* %1, align 4
  br label %16, !llvm.loop !8

24:                                               ; preds = %16
  br label %25

25:                                               ; preds = %24, %12
  %26 = load i32, i32* %2, align 4
  ret i32 %26
}

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  %2 = call i32 @ifWhile()
  ret i32 %2
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 14.0.0-1ubuntu1.1"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
