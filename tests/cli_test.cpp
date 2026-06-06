#include <gtest/gtest.h>

#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <string>

// WCL_BINARY is defined by CMake as the absolute path to the built `wcl`
// executable. These are end-to-end tests of main()'s argument parsing and
// output formatting -- the parts not reachable as a plain function call.
#ifndef WCL_BINARY
#error "WCL_BINARY must be defined by the build system"
#endif

namespace {

struct CmdResult
{
  std::string out;
  int exitCode;
};

// Run a shell command, capturing stdout and the process exit code.
CmdResult run( const std::string& cmd )
{
  std::array<char, 4096> buf{};
  std::string out;
  FILE* pipe = ::popen( cmd.c_str(), "r" );
  if ( !pipe ) return { "", -1 };
  size_t n;
  while ( ( n = ::fread( buf.data(), 1, buf.size(), pipe ) ) > 0 )
    out.append( buf.data(), n );
  int status = ::pclose( pipe );
  const int code = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
  return { out, code };
}

const std::string kBin = WCL_BINARY;

// Build a `printf '...' | wcl ...` pipeline so we control stdin precisely.
std::string piped( const std::string& stdinPayload, const std::string& args )
{
  // %b makes printf interpret backslash escapes like \n.
  return "printf '%b' '" + stdinPayload + "' | " + kBin + " " + args;
}

}  // namespace

// ---------------------------------------------------------------------------
// stdin (no file arguments) -> single number, newline-terminated.
// ---------------------------------------------------------------------------
TEST( Cli, StdinDefaultNewline )
{
  CmdResult r = run( piped( "a\\nb\\nc\\n", "" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "3\n" );
}

TEST( Cli, StdinEmpty )
{
  CmdResult r = run( piped( "", "" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "0\n" );
}

// ---------------------------------------------------------------------------
// --char selects an arbitrary byte to count.
// ---------------------------------------------------------------------------
TEST( Cli, CharFlagSelectsTarget )
{
  CmdResult r = run( piped( "a,b,c,d", "--char ," ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "3\n" );
}

TEST( Cli, CharFlagUsesFirstCharOnly )
{
  CmdResult r = run( piped( "axbxc", "--char xyz" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "2\n" );
}

TEST( Cli, CharFlagMissingValueErrors )
{
  CmdResult r = run( kBin + " --char </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

TEST( Cli, CharFlagEmptyValueErrors )
{
  CmdResult r = run( kBin + " --char '' </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// --bytes-per-thread parsing and validation.
// ---------------------------------------------------------------------------
TEST( Cli, BytesPerThreadAcceptedSmall )
{
  CmdResult r = run( piped( "a\\nb\\nc\\nd\\n", "--bytes-per-thread 2" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "4\n" );
}

TEST( Cli, BytesPerThreadZeroErrors )
{
  CmdResult r = run( kBin + " --bytes-per-thread 0 </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

TEST( Cli, BytesPerThreadMissingValueErrors )
{
  CmdResult r = run( kBin + " --bytes-per-thread </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// Unknown flags are rejected.
// ---------------------------------------------------------------------------
TEST( Cli, UnknownFlagErrors )
{
  CmdResult r = run( kBin + " --nope </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// Combined flags.
// ---------------------------------------------------------------------------
TEST( Cli, CombinedFlags )
{
  CmdResult r = run( piped( "a;b;c;d;e", "--bytes-per-thread 3 --char ';'" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "4\n" );
}

// ---------------------------------------------------------------------------
// Single file argument: prints only the grand total (no per-file line).
// ---------------------------------------------------------------------------
TEST( Cli, SingleFileTotalOnly )
{
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/wcl_cli_one.txt && ";
  CmdResult r = run( create + kBin + " /tmp/wcl_cli_one.txt; rm -f /tmp/wcl_cli_one.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "3\n" );
}

// ---------------------------------------------------------------------------
// Multiple files: a per-file "<count> <name>" line each, then the grand total.
// File processing is dispatched across threads, but output order is by index.
// ---------------------------------------------------------------------------
TEST( Cli, MultipleFilesPerFileAndTotal )
{
  std::string setup =
      "printf '%b' 'a\\nb\\n' > /tmp/wcl_cli_a.txt && "
      "printf '%b' 'x\\ny\\nz\\n' > /tmp/wcl_cli_b.txt && ";
  std::string teardown = "; rm -f /tmp/wcl_cli_a.txt /tmp/wcl_cli_b.txt";
  CmdResult r = run( setup + kBin +
                     " /tmp/wcl_cli_a.txt /tmp/wcl_cli_b.txt" + teardown );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "2 /tmp/wcl_cli_a.txt\n"
             "3 /tmp/wcl_cli_b.txt\n"
             "5\n" );
}
