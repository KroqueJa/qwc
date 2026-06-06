#include <gtest/gtest.h>

#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <cstdlib>
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

// Build a deterministic directory tree under /tmp and return its root path.
// top.txt -> 2 lines, sub/mid.txt -> 3, sub/deep/bottom.txt -> 4 (total 9).
std::string makeRecTree( const std::string& root )
{
  std::string cmd =
      "rm -rf " + root + " && "
      "mkdir -p " + root + "/sub/deep && "
      "printf '%b' 'a\\nb\\n'       > " + root + "/top.txt && "
      "printf '%b' 'x\\ny\\nz\\n'   > " + root + "/sub/mid.txt && "
      "printf '%b' '1\\n2\\n3\\n4\\n' > " + root + "/sub/deep/bottom.txt";
  std::system( cmd.c_str() );
  return root;
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
// --help prints usage and exits 0, without reading or counting anything.
// ---------------------------------------------------------------------------
TEST( Cli, HelpPrintsUsageAndExitsZero )
{
  CmdResult r = run( kBin + " --help </dev/null" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
  EXPECT_NE( r.out.find( "--recursive" ), std::string::npos );
  EXPECT_NE( r.out.find( "--char" ), std::string::npos );
}

TEST( Cli, HelpDoesNotConsumeStdin )
{
  // Even with data on stdin, --help shows help rather than a count.
  CmdResult r = run( piped( "a\\nb\\nc\\n", "--help" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
  EXPECT_EQ( r.out.find( "3\n" ), std::string::npos );  // no count emitted
}

TEST( Cli, NoArgsDoesNotDefaultToHelp )
{
  // Unlike many tools, bare `wcl` must read stdin (wc-compatible), not help.
  CmdResult r = run( piped( "a\\nb\\n", "" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "2\n" );
  EXPECT_EQ( r.out.find( "Usage:" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// Short-flag aliases: -h/-r/-c mirror their long forms (no short form for
// --bytes-per-thread by design).
// ---------------------------------------------------------------------------
TEST( CliShortFlags, DashHIsHelp )
{
  CmdResult r = run( kBin + " -h </dev/null" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
}

TEST( CliShortFlags, DashCIsChar )
{
  CmdResult r = run( piped( "a,b,c,d", "-c ," ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "3\n" );
}

TEST( CliShortFlags, DashCMissingValueErrors )
{
  CmdResult r = run( kBin + " -c </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

TEST( CliShortFlags, DashRRecursesDirectory )
{
  const std::string root = makeRecTree( "/tmp/wcl_short_r" );
  CmdResult r = run( kBin + " -r " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "4 /tmp/wcl_short_r/sub/deep/bottom.txt\n"
             "3 /tmp/wcl_short_r/sub/mid.txt\n"
             "2 /tmp/wcl_short_r/top.txt\n"
             "9\n" );
}

TEST( CliShortFlags, ShortFlagsCombine )
{
  const std::string root = makeRecTree( "/tmp/wcl_short_rc" );
  CmdResult r = run( kBin + " -c x -r " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "0 /tmp/wcl_short_rc/sub/deep/bottom.txt\n"
             "1 /tmp/wcl_short_rc/sub/mid.txt\n"
             "0 /tmp/wcl_short_rc/top.txt\n"
             "1\n" );
}

TEST( CliShortFlags, NoShortFormForBytesPerThread )
{
  // -b is not a recognized flag; it must be rejected, not silently accepted.
  CmdResult r = run( kBin + " -b 2 </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
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

// ---------------------------------------------------------------------------
// --recursive: directory arguments expand to all regular files beneath them,
// sorted, with a per-file line each and a grand total.
// ---------------------------------------------------------------------------
TEST( CliRecursive, ExpandsDirectorySortedWithTotal )
{
  const std::string root = makeRecTree( "/tmp/wcl_rec_cli" );
  CmdResult r = run( kBin + " --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "4 /tmp/wcl_rec_cli/sub/deep/bottom.txt\n"
             "3 /tmp/wcl_rec_cli/sub/mid.txt\n"
             "2 /tmp/wcl_rec_cli/top.txt\n"
             "9\n" );
}

TEST( CliRecursive, ComposesWithCharFlag )
{
  const std::string root = makeRecTree( "/tmp/wcl_rec_cli2" );
  CmdResult r = run( kBin + " --char x --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "0 /tmp/wcl_rec_cli2/sub/deep/bottom.txt\n"
             "1 /tmp/wcl_rec_cli2/sub/mid.txt\n"
             "0 /tmp/wcl_rec_cli2/top.txt\n"
             "1\n" );
}

TEST( CliRecursive, EmptyDirectoryPrintsZero )
{
  const std::string root = "/tmp/wcl_rec_cli_empty";
  std::system( ( "rm -rf " + root + " && mkdir -p " + root ).c_str() );
  CmdResult r = run( kBin + " --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "0\n" );
}

TEST( CliRecursive, PreservesTopLevelOrderForMixedArgs )
{
  const std::string root = makeRecTree( "/tmp/wcl_rec_cli3" );
  // A plain file first, then a directory: file stays first, dir expands sorted.
  CmdResult r = run( kBin + " --recursive " + root + "/top.txt " + root + "/sub" );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             "2 /tmp/wcl_rec_cli3/top.txt\n"
             "4 /tmp/wcl_rec_cli3/sub/deep/bottom.txt\n"
             "3 /tmp/wcl_rec_cli3/sub/mid.txt\n"
             "9\n" );
}

TEST( CliRecursive, SingleFileArgStillTotalOnly )
{
  // --recursive with a non-directory arg behaves like a normal single file.
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/wcl_rec_one.txt && ";
  CmdResult r = run( create + kBin + " --recursive /tmp/wcl_rec_one.txt"
                     "; rm -f /tmp/wcl_rec_one.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, "3\n" );
}
