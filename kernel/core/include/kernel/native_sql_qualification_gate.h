#pragma once

namespace kernel {
namespace native_sql_qualification_gate {

enum OverallResult {
    OverallNotRun,
    OverallPassed,
    OverallFailed
};

template <typename SqlDiagnostic>
bool RunSqlDiagnosticAfterMapping(bool mappingPassed, SqlDiagnostic diagnostic)
{
    return mappingPassed ? diagnostic() : false;
}

inline OverallResult CombineResults(bool mappingPassed, bool sqlPassed)
{
    return mappingPassed && sqlPassed ? OverallPassed : OverallFailed;
}

} // namespace native_sql_qualification_gate
} // namespace kernel
