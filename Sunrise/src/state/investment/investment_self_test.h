#pragma once

namespace sunrise::state::investment::store::self_test {

/**
 * Round-trips a mutated copy of the authored-default account through a disposable database file
 * -- never the real save -- using the exact same `open`/`write_account`/`read_account` this
 * module uses for real play, and reports the first field that comes back different. This is a
 * regression check for the store's own read/write code (a field serialized as the wrong type, a
 * new field never wired into the writer), independent of anything that could go wrong with a
 * real, already-committed database file. Runs once per process, before the real database opens.
 * @param module The loaded DLL module, the same one `initialize` receives.
 */
void run(void* module) noexcept;

} // namespace sunrise::state::investment::store::self_test
