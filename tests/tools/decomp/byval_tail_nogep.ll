; ModuleID = 'byval_tail_nogep.cpp'
source_filename = "byval_tail_nogep.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.big_base = type { [32 x i32] }

; Function Attrs: uwtable mustprogress
define dso_local double @_Z3foo8big_base(%struct.big_base* nocapture readonly byval(%struct.big_base) align 8 %x) local_unnamed_addr #0 !dbg !7 {
entry:
  call void @llvm.dbg.declare(metadata %struct.big_base* %x, metadata !20, metadata !DIExpression()), !dbg !21
  %call = tail call double @_Z3bar8big_base(%struct.big_base* nonnull byval(%struct.big_base) align 8 %x), !dbg !22
  ret double %call, !dbg !23
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare !dbg !24 dso_local double @_Z3bar8big_base(%struct.big_base* byval(%struct.big_base) align 8) local_unnamed_addr #2

attributes #0 = { uwtable mustprogress "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, producer: "clang version 12.0.1 (https://github.com/microsoft/vcpkg.git 2a31089e777fc187f1cc05338250b8e1810cfb52)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "byval_tail_nogep.cpp", directory: "/")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 12.0.1 (https://github.com/microsoft/vcpkg.git 2a31089e777fc187f1cc05338250b8e1810cfb52)"}
!7 = distinct !DISubprogram(name: "foo", linkageName: "_Z3foo8big_base", scope: !8, file: !8, line: 5, type: !9, scopeLine: 5, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !19)
!8 = !DIFile(filename: "byval_tail_nogep.cpp", directory: "/")
!9 = !DISubroutineType(types: !10)
!10 = !{!11, !12}
!11 = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
!12 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "big_base", file: !8, line: 1, size: 1024, flags: DIFlagTypePassByValue, elements: !13, identifier: "_ZTS8big_base")
!13 = !{!14}
!14 = !DIDerivedType(tag: DW_TAG_member, name: "x", scope: !12, file: !8, line: 2, baseType: !15, size: 1024)
!15 = !DICompositeType(tag: DW_TAG_array_type, baseType: !16, size: 1024, elements: !17)
!16 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!17 = !{!18}
!18 = !DISubrange(count: 32)
!19 = !{!20}
!20 = !DILocalVariable(name: "x", arg: 1, scope: !7, file: !8, line: 5, type: !12)
!21 = !DILocation(line: 5, column: 28, scope: !7)
!22 = !DILocation(line: 6, column: 10, scope: !7)
!23 = !DILocation(line: 6, column: 3, scope: !7)
!24 = !DISubprogram(name: "bar", linkageName: "_Z3bar8big_base", scope: !8, file: !8, line: 4, type: !9, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized, retainedNodes: !2)
