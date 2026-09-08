// Deliberately signed without com.apple.security.get-task-allow (see
// [template.machbug_fixture_no_task_allow] in cmake.toml). Its only purpose is to exist as a
// valid, spawnable, ad-hoc-signed binary that task_for_pid is expected to be denied against --
// what happens after that denial is never reached, so the body does nothing.
int main() { return 0; }
