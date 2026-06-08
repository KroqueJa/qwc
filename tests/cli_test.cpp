#include <gtest/gtest.h>

#include <sys/wait.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

// QWC_BINARY is defined by CMake as the absolute path to the built `qwc`
// executable. These are end-to-end tests of main()'s argument parsing and
// output formatting -- the parts not reachable as a plain function call.
#ifndef QWC_BINARY
#error "QWC_BINARY must be defined by the build system"
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

const std::string kBin = QWC_BINARY;

// Build a `printf '...' | qwc ...` pipeline so we control stdin precisely.
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

// Format one single-column output line the way wc/qwc do: " %7ju %s\n" with the
// name, or " %7ju\n" without one. Lets the structural tests assert exact bytes
// without hand-counting padding.
std::string line( uintmax_t count, const std::string& name = "" )
{
  std::ostringstream os;
  os << ' ' << std::setw( 7 ) << count;
  if ( !name.empty() ) os << ' ' << name;
  os << '\n';
  return os.str();
}

// The three-column form bare `wc`/`qwc` print: lines, words, bytes.
std::string allLine( uintmax_t l, uintmax_t w, uintmax_t b,
                     const std::string& name = "" )
{
  std::ostringstream os;
  os << ' ' << std::setw( 7 ) << l << ' ' << std::setw( 7 ) << w << ' '
     << std::setw( 7 ) << b;
  if ( !name.empty() ) os << ' ' << name;
  os << '\n';
  return os.str();
}

}  // namespace

// ---------------------------------------------------------------------------
// No count flag: like bare `wc`, qwc prints lines, words and bytes. From stdin
// there is no filename, just the three padded numbers.
// ---------------------------------------------------------------------------
TEST( Cli, StdinDefaultIsLinesWordsBytes )
{
  CmdResult r = run( piped( "a\\nb\\nc\\n", "" ) );  // 3 lines, 3 words, 6 bytes
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, allLine( 3, 3, 6 ) );
}

TEST( Cli, StdinEmpty )
{
  CmdResult r = run( piped( "", "" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, allLine( 0, 0, 0 ) );
}

TEST( Cli, NoArgsDoesNotDefaultToHelp )
{
  // Unlike many tools, bare `qwc` must read stdin (wc-compatible), not help.
  CmdResult r = run( piped( "a\\nb\\n", "" ) );  // 2 lines, 2 words, 4 bytes
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, allLine( 2, 2, 4 ) );
  EXPECT_EQ( r.out.find( "Usage:" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// --help prints usage and exits 0, without reading or counting anything.
// ---------------------------------------------------------------------------
TEST( Cli, HelpPrintsUsageAndExitsZero )
{
  CmdResult r = run( kBin + " --help </dev/null" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
  EXPECT_NE( r.out.find( "--lines" ), std::string::npos );
  EXPECT_NE( r.out.find( "--words" ), std::string::npos );
  EXPECT_NE( r.out.find( "--bytes" ), std::string::npos );
  EXPECT_NE( r.out.find( "--chars" ), std::string::npos );
  EXPECT_NE( r.out.find( "--max-line-length" ), std::string::npos );
  EXPECT_NE( r.out.find( "--char " ), std::string::npos );  // the qwc extension
  EXPECT_NE( r.out.find( "--recursive" ), std::string::npos );
}

TEST( Cli, HelpDoesNotConsumeStdin )
{
  // Even with data on stdin, --help shows help rather than a count.
  CmdResult r = run( piped( "a\\nb\\nc\\n", "--help" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// Count flags select a single column, mirroring `wc`. Short and long forms.
// ---------------------------------------------------------------------------
TEST( CliShortFlags, DashHIsHelp )
{
  CmdResult r = run( kBin + " -h </dev/null" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_NE( r.out.find( "Usage:" ), std::string::npos );
}

TEST( CliShortFlags, DashLCountsLines )
{
  CmdResult r = run( piped( "a\\nb\\nc\\n", "-l" ) );  // 3 newlines
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 3 ) );
}

TEST( Cli, LinesLongForm )
{
  CmdResult r = run( piped( "a\\nb\\nc\\nd\\n", "--lines" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
}

TEST( CliShortFlags, DashWCountsWords )
{
  CmdResult r = run( piped( "  the quick brown   fox\\n", "-w" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
}

TEST( Cli, WordsLongForm )
{
  CmdResult r = run( piped( "  the quick brown   fox\\n", "--words" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
}

// -c counts bytes (like `wc -c`); its long form is --bytes.
TEST( CliShortFlags, DashCCountsBytes )
{
  CmdResult r = run( piped( "hello world", "-c" ) );  // 11 bytes
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 11 ) );
}

TEST( Cli, BytesLongForm )
{
  CmdResult r = run( piped( "abcde", "--bytes" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 5 ) );
}

TEST( Cli, BytesFlagCountsSizeFile )
{
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/qwc_bytes.txt && ";
  CmdResult r = run( create + kBin + " --bytes /tmp/qwc_bytes.txt"
                     "; rm -f /tmp/qwc_bytes.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 6, "/tmp/qwc_bytes.txt" ) );  // 6 bytes
}

// -c takes no value: a following argument is a file, not consumed as a value.
TEST( CliShortFlags, DashCTakesNoValue )
{
  std::string create = "printf '%b' 'abc' > /tmp/qwc_dashc.txt && ";
  CmdResult r = run( create + kBin + " -c /tmp/qwc_dashc.txt"
                     "; rm -f /tmp/qwc_dashc.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  // 3 bytes; the path was not swallowed (and a single file still names itself).
  EXPECT_EQ( r.out, line( 3, "/tmp/qwc_dashc.txt" ) );
}

// -m counts characters (like `wc -m`); its long form is --chars. On ASCII a
// character is a byte, so this is locale-independent.
TEST( CliShortFlags, DashMCountsChars )
{
  CmdResult r = run( piped( "hello world", "-m" ) );  // 11 ASCII chars
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 11 ) );
}

TEST( Cli, CharsLongFormCountsChars )
{
  CmdResult r = run( piped( "abcde", "--chars" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 5 ) );
}

// -L reports the longest line in bytes (like `wc -L`).
TEST( CliShortFlags, DashLLongestLine )
{
  CmdResult r = run( piped( "ab\\nabcd\\nx\\n", "-L" ) );  // "abcd" -> 4
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
}

TEST( Cli, MaxLineLengthLongForm )
{
  CmdResult r = run( piped( "a\\nbbbbb\\ncc\\n", "--max-line-length" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 5 ) );  // "bbbbb"
}

TEST( CliShortFlags, NoShortFormForBytesPerThread )
{
  // -b is not a recognized flag; it must be rejected, not silently accepted.
  CmdResult r = run( kBin + " -b 2 </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// --char selects an arbitrary byte to count (a qwc extension, not in wc).
// ---------------------------------------------------------------------------
TEST( Cli, CharFlagSelectsTarget )
{
  CmdResult r = run( piped( "a,b,c,d", "--char ," ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 3 ) );
}

TEST( Cli, CharFlagUsesFirstCharOnly )
{
  CmdResult r = run( piped( "axbxc", "--char xyz" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 2 ) );
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

TEST( Cli, CharsAndCharAreDistinct )
{
  // --char (with a value) counts a chosen byte; --chars (no value) counts
  // characters. The shared prefix must not make the parser confuse them.
  CmdResult r = run( piped( "a,b,c", "--char ," ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 2 ) );  // two commas, not the 5-char count
}

// ---------------------------------------------------------------------------
// Word counting across a tiny bytes-per-thread, so the file is split into many
// chunks: the per-chunk merge must not drop or double-count boundary words.
// ---------------------------------------------------------------------------
TEST( Cli, WordsFlagChunkedMatchesTotal )
{
  std::string create =
      "printf '%b' 'aa bb cc dd ee ff gg hh ii jj' > /tmp/qwc_w_big.txt && ";
  CmdResult r = run( create + kBin + " -w --bytes-per-thread 4"
                     " /tmp/qwc_w_big.txt; rm -f /tmp/qwc_w_big.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 10, "/tmp/qwc_w_big.txt" ) );
}

TEST( CliRecursive, ComposesWithWordsFlag )
{
  const std::string root = makeRecTree( "/tmp/qwc_rec_words" );
  // One token per line: top=2 words, mid=3, bottom=4 -> 9 total.
  CmdResult r = run( kBin + " -w --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_rec_words/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_rec_words/sub/mid.txt" ) +
             line( 2, "/tmp/qwc_rec_words/top.txt" ) +
             line( 9, "total" ) );
}

// ---------------------------------------------------------------------------
// --max-line-length across files: the "total" is the maximum, not a sum.
// ---------------------------------------------------------------------------
TEST( Cli, MaxLineLengthMultiFileTotalIsMax )
{
  std::string setup =
      "printf '%b' 'a\\nbbb\\n'    > /tmp/qwc_L_a.txt && "  // longest line 3
      "printf '%b' 'cccccc\\nd\\n' > /tmp/qwc_L_b.txt && ";  // longest line 6
  std::string teardown = "; rm -f /tmp/qwc_L_a.txt /tmp/qwc_L_b.txt";
  CmdResult r = run( setup + kBin + " -L"
                     " /tmp/qwc_L_a.txt /tmp/qwc_L_b.txt" + teardown );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 3, "/tmp/qwc_L_a.txt" ) +
             line( 6, "/tmp/qwc_L_b.txt" ) +
             line( 6, "total" ) );  // max, not sum (9)
}

TEST( Cli, MaxLineLengthChunkedMatchesTotal )
{
  std::string create =
      "printf '%b' 'aa\\nbbbbbbbbbbbbbbbbbbbb\\ncc' > /tmp/qwc_L_big.txt && ";
  CmdResult r = run( create + kBin + " -L --bytes-per-thread 4"
                     " /tmp/qwc_L_big.txt; rm -f /tmp/qwc_L_big.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 20, "/tmp/qwc_L_big.txt" ) );  // the 20 b's
}

// ---------------------------------------------------------------------------
// --bytes-per-thread parsing and validation.
// ---------------------------------------------------------------------------
TEST( Cli, BytesPerThreadAcceptedSmall )
{
  CmdResult r = run( piped( "a\\nb\\nc\\nd\\n", "-l --bytes-per-thread 2" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
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
// Unknown flags are rejected -- including -a, which qwc no longer has (bare
// qwc is the lines/words/bytes view, exactly like wc).
// ---------------------------------------------------------------------------
TEST( Cli, UnknownFlagErrors )
{
  CmdResult r = run( kBin + " --nope </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

TEST( Cli, DashAIsNotAFlag )
{
  CmdResult r = run( kBin + " -a </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// Combined options: a tuning flag plus a count flag.
// ---------------------------------------------------------------------------
TEST( Cli, CombinedFlags )
{
  CmdResult r = run( piped( "a;b;c;d;e", "--bytes-per-thread 3 --char ';'" ) );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 4 ) );
}

// ---------------------------------------------------------------------------
// Single file argument: one line with the count and the file name, no total --
// exactly like `wc`. (-l keeps it a single column.)
// ---------------------------------------------------------------------------
TEST( Cli, SingleFileCountAndName )
{
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/qwc_cli_one.txt && ";
  CmdResult r = run( create + kBin + " -l /tmp/qwc_cli_one.txt; rm -f /tmp/qwc_cli_one.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 3, "/tmp/qwc_cli_one.txt" ) );
}

// Default (no flag) single file: three columns, then the name.
TEST( Cli, SingleFileDefaultThreeColumns )
{
  std::string create = "printf '%b' 'a b\\nc\\n' > /tmp/qwc_cli_def.txt && ";
  CmdResult r = run( create + kBin + " /tmp/qwc_cli_def.txt; rm -f /tmp/qwc_cli_def.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  // 2 lines, 3 words, 6 bytes.
  EXPECT_EQ( r.out, allLine( 2, 3, 6, "/tmp/qwc_cli_def.txt" ) );
}

// ---------------------------------------------------------------------------
// Multiple files: a per-file "<count> <name>" line each, then the grand total.
// File processing is dispatched across threads, but output order is by index.
// ---------------------------------------------------------------------------
TEST( Cli, MultipleFilesPerFileAndTotal )
{
  std::string setup =
      "printf '%b' 'a\\nb\\n' > /tmp/qwc_cli_a.txt && "
      "printf '%b' 'x\\ny\\nz\\n' > /tmp/qwc_cli_b.txt && ";
  std::string teardown = "; rm -f /tmp/qwc_cli_a.txt /tmp/qwc_cli_b.txt";
  CmdResult r = run( setup + kBin + " -l"
                     " /tmp/qwc_cli_a.txt /tmp/qwc_cli_b.txt" + teardown );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_cli_a.txt" ) +
             line( 3, "/tmp/qwc_cli_b.txt" ) +
             line( 5, "total" ) );
}

// ---------------------------------------------------------------------------
// --recursive: directory arguments expand to all regular files beneath them,
// sorted, with a per-file line each and a grand total. (-l = line counts.)
// ---------------------------------------------------------------------------
TEST( CliRecursive, ExpandsDirectorySortedWithTotal )
{
  const std::string root = makeRecTree( "/tmp/qwc_rec_cli" );
  CmdResult r = run( kBin + " -l --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_rec_cli/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_rec_cli/sub/mid.txt" ) +
             line( 2, "/tmp/qwc_rec_cli/top.txt" ) +
             line( 9, "total" ) );
}

TEST( CliRecursive, ComposesWithCharFlag )
{
  const std::string root = makeRecTree( "/tmp/qwc_rec_cli2" );
  CmdResult r = run( kBin + " --char x --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 0, "/tmp/qwc_rec_cli2/sub/deep/bottom.txt" ) +
             line( 1, "/tmp/qwc_rec_cli2/sub/mid.txt" ) +
             line( 0, "/tmp/qwc_rec_cli2/top.txt" ) +
             line( 1, "total" ) );
}

TEST( CliRecursive, EmptyDirectoryPrintsZero )
{
  const std::string root = "/tmp/qwc_rec_cli_empty";
  std::system( ( "rm -rf " + root + " && mkdir -p " + root ).c_str() );
  CmdResult r = run( kBin + " -l --recursive " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 0 ) );  // no files matched -> bare padded zero
}

TEST( CliRecursive, PreservesTopLevelOrderForMixedArgs )
{
  const std::string root = makeRecTree( "/tmp/qwc_rec_cli3" );
  // A plain file first, then a directory: file stays first, dir expands sorted.
  CmdResult r = run( kBin + " -l --recursive " + root + "/top.txt " + root + "/sub" );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_rec_cli3/top.txt" ) +
             line( 4, "/tmp/qwc_rec_cli3/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_rec_cli3/sub/mid.txt" ) +
             line( 9, "total" ) );
}

TEST( CliRecursive, SingleFileArgNoTotalLine )
{
  // --recursive with a non-directory arg behaves like a normal single file.
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/qwc_rec_one.txt && ";
  CmdResult r = run( create + kBin + " -l --recursive /tmp/qwc_rec_one.txt"
                     "; rm -f /tmp/qwc_rec_one.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 3, "/tmp/qwc_rec_one.txt" ) );
}

// ---------------------------------------------------------------------------
// --sort-by-count: orders the whole flat file list by count ascending (biggest
// at the bottom, next to the grand total), tie-broken alphabetically. Sorting
// applies to the single-column views, so these run with -l.
// ---------------------------------------------------------------------------
namespace {

std::string makeSortTree( const std::string& root )
{
  makeRecTree( root );
  std::system(
      ( "printf '%b' 'solo\\n' > " + root + "/zzz_one.txt" ).c_str() );
  return root;
}

}  // namespace

TEST( CliSortByCount, OrdersAscendingAcrossDirectories )
{
  const std::string root = makeSortTree( "/tmp/qwc_sort_cnt" );
  CmdResult r = run( kBin + " -l --recursive --sort-by-count " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 1, "/tmp/qwc_sort_cnt/zzz_one.txt" ) +
             line( 2, "/tmp/qwc_sort_cnt/top.txt" ) +
             line( 3, "/tmp/qwc_sort_cnt/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_sort_cnt/sub/deep/bottom.txt" ) +
             line( 10, "total" ) );
}

TEST( CliSortByCount, WorksWithoutRecursive )
{
  std::string setup =
      "printf '%b' 'x\\ny\\nz\\nw\\n' > /tmp/qwc_sc_big.txt && "
      "printf '%b' 'a\\nb\\n'         > /tmp/qwc_sc_small.txt && ";
  std::string teardown = "; rm -f /tmp/qwc_sc_big.txt /tmp/qwc_sc_small.txt";
  CmdResult r = run( setup + kBin + " -l --sort-by-count"
                     " /tmp/qwc_sc_big.txt /tmp/qwc_sc_small.txt" + teardown );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_sc_small.txt" ) +
             line( 4, "/tmp/qwc_sc_big.txt" ) +
             line( 6, "total" ) );
}

TEST( CliSortByCount, EqualCountsTieBreakAlphabetically )
{
  std::string setup =
      "printf '%b' 'a\\nb\\n' > /tmp/qwc_sc_charlie.txt && "
      "printf '%b' 'a\\nb\\n' > /tmp/qwc_sc_alpha.txt && "
      "printf '%b' 'a\\nb\\n' > /tmp/qwc_sc_bravo.txt && ";
  std::string teardown =
      "; rm -f /tmp/qwc_sc_charlie.txt /tmp/qwc_sc_alpha.txt "
      "/tmp/qwc_sc_bravo.txt";
  CmdResult r = run( setup + kBin + " -l --sort-by-count"
                     " /tmp/qwc_sc_charlie.txt /tmp/qwc_sc_bravo.txt"
                     " /tmp/qwc_sc_alpha.txt" + teardown );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_sc_alpha.txt" ) +
             line( 2, "/tmp/qwc_sc_bravo.txt" ) +
             line( 2, "/tmp/qwc_sc_charlie.txt" ) +
             line( 6, "total" ) );
}

TEST( CliSortByCount, SingleFileNoTotalLine )
{
  std::string create = "printf '%b' 'a\\nb\\nc\\n' > /tmp/qwc_sc_one.txt && ";
  CmdResult r = run( create + kBin + " -l --sort-by-count /tmp/qwc_sc_one.txt"
                     "; rm -f /tmp/qwc_sc_one.txt" );
  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out, line( 3, "/tmp/qwc_sc_one.txt" ) );
}

// ---------------------------------------------------------------------------
// --sort-by-name / --sort-by-size. makeRecTree gives, by name:
//   sub/deep/bottom.txt (4 lines, 8 bytes)
//   sub/mid.txt         (3 lines, 6 bytes)
//   top.txt             (2 lines, 4 bytes)
// so name order and size order are reverses of one another here.
// ---------------------------------------------------------------------------
TEST( CliSort, ByNameAlphabetical )
{
  const std::string root = makeRecTree( "/tmp/qwc_sort_name" );
  CmdResult r = run( kBin + " -l -r --sort-by-name " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_sort_name/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_sort_name/sub/mid.txt" ) +
             line( 2, "/tmp/qwc_sort_name/top.txt" ) +
             line( 9, "total" ) );
}

TEST( CliSort, BySizeAscending )
{
  const std::string root = makeRecTree( "/tmp/qwc_sort_size" );
  CmdResult r = run( kBin + " -l -r --sort-by-size " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_sort_size/top.txt" ) +
             line( 3, "/tmp/qwc_sort_size/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_sort_size/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );
}

// ---------------------------------------------------------------------------
// --reverse flips whichever order is active.
// ---------------------------------------------------------------------------
TEST( CliReverse, FlipsCountOrderBiggestFirst )
{
  const std::string root = makeRecTree( "/tmp/qwc_rev_count" );
  CmdResult r = run( kBin + " -l -r --sort-by-count --reverse " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_rev_count/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_rev_count/sub/mid.txt" ) +
             line( 2, "/tmp/qwc_rev_count/top.txt" ) +
             line( 9, "total" ) );
}

TEST( CliReverse, AloneFlipsDefaultAlphabetical )
{
  const std::string root = makeRecTree( "/tmp/qwc_rev_alpha" );
  CmdResult r = run( kBin + " -l -r --reverse " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_rev_alpha/top.txt" ) +
             line( 3, "/tmp/qwc_rev_alpha/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_rev_alpha/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );
}

// ---------------------------------------------------------------------------
// --top N keeps the N highest-ranked files; the total still covers all of them.
// ---------------------------------------------------------------------------
TEST( CliTop, KeepsLargestByCountAscending )
{
  const std::string root = makeRecTree( "/tmp/qwc_top_count" );
  CmdResult r = run( kBin + " -l -r --top 2 " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 3, "/tmp/qwc_top_count/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_top_count/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );  // total covers all three files
}

TEST( CliTop, WithReverseBiggestFirst )
{
  const std::string root = makeRecTree( "/tmp/qwc_top_rev" );
  CmdResult r = run( kBin + " -l -r --top 2 --reverse " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_top_rev/sub/deep/bottom.txt" ) +
             line( 3, "/tmp/qwc_top_rev/sub/mid.txt" ) +
             line( 9, "total" ) );
}

TEST( CliTop, OneFileTotalStillCoversAll )
{
  const std::string root = makeRecTree( "/tmp/qwc_top_one" );
  CmdResult r = run( kBin + " -l -r --top 1 " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 4, "/tmp/qwc_top_one/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );
}

TEST( CliTop, LargerThanCountShowsAll )
{
  const std::string root = makeRecTree( "/tmp/qwc_top_all" );
  CmdResult r = run( kBin + " -l -r --top 99 " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 2, "/tmp/qwc_top_all/top.txt" ) +
             line( 3, "/tmp/qwc_top_all/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_top_all/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );
}

TEST( CliTop, ComposesWithSortBySize )
{
  const std::string root = makeRecTree( "/tmp/qwc_top_size" );
  // Two largest by size are mid (6B) and bottom (8B), shown ascending by size.
  CmdResult r = run( kBin + " -l -r --sort-by-size --top 2 " + root );
  std::system( ( "rm -rf " + root ).c_str() );

  EXPECT_EQ( r.exitCode, 0 );
  EXPECT_EQ( r.out,
             line( 3, "/tmp/qwc_top_size/sub/mid.txt" ) +
             line( 4, "/tmp/qwc_top_size/sub/deep/bottom.txt" ) +
             line( 9, "total" ) );
}

TEST( CliTop, ZeroErrors )
{
  CmdResult r = run( kBin + " --top 0 </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

TEST( CliTop, MissingValueErrors )
{
  CmdResult r = run( kBin + " --top </dev/null 2>/dev/null" );
  EXPECT_EQ( r.exitCode, 1 );
}

// ---------------------------------------------------------------------------
// Drop-in compatibility: with the system `wc` as the ground truth, qwc's output
// must match byte-for-byte. Bare qwc == bare wc (lines, words, bytes); -l == wc
// -l; -w == wc -w; -c == wc -c; -m == wc -m; -L == wc -L. This guards the exact
// wc formatting (leading space, min-width-7 fields, filename, "total" line)
// without the test re-implementing it.
// ---------------------------------------------------------------------------
namespace {

struct Mode
{
  const char* qwcFlag;  // how qwc selects this mode ("" == default: l w c)
  const char* wcFlag;   // the matching wc flag ("" == bare wc)
};
const Mode kCoreModes[] = { { "", "" },     { "-l", "-l" }, { "-w", "-w" },
                            { "-c", "-c" },  { "-m", "-m" }, { "-L", "-L" } };

}  // namespace

TEST( CliWcCompat, SingleFileMatchesWc )
{
  const std::string f = "/tmp/qwc_wc_one.txt";
  // Mixed content: words, blank line, trailing newline (ASCII, so -w/-m agree
  // with wc under any locale).
  std::system(
      ( "printf '%b' 'alpha beta\\ngamma\\n\\nx y z\\n' > " + f ).c_str() );
  for ( const Mode& m : kCoreModes ) {
    CmdResult got = run( kBin + " " + m.qwcFlag + " " + f );
    CmdResult exp = run( std::string( "wc " ) + m.wcFlag + " " + f );
    EXPECT_EQ( got.exitCode, 0 );
    EXPECT_EQ( got.out, exp.out )
        << "qwc '" << m.qwcFlag << "' vs wc '" << m.wcFlag << "'";
  }
  std::system( ( "rm -f " + f ).c_str() );
}

TEST( CliWcCompat, StdinMatchesWc )
{
  const std::string payload = "alpha beta\\ngamma\\n\\nx y z\\n";
  for ( const Mode& m : kCoreModes ) {
    CmdResult got = run( piped( payload, m.qwcFlag ) );
    CmdResult exp =
        run( "printf '%b' '" + payload + "' | wc " + m.wcFlag );
    EXPECT_EQ( got.out, exp.out )
        << "qwc '" << m.qwcFlag << "' vs wc '" << m.wcFlag << "'";
  }
}

// Multibyte: qwc -m must match wc -m byte-for-byte on real UTF-8 content. Both
// honor the ambient locale (code points in a UTF-8 locale, bytes under C), so
// they agree regardless of which locale the test happens to run under. The
// payload is written as literal UTF-8 bytes to sidestep printf escape quirks.
TEST( CliWcCompat, MultibyteCharsMatchWc )
{
  const std::string f = "/tmp/qwc_wc_utf8.txt";
  // "héllo wörld ☃\n" -- accented letters (2 bytes each) and a snowman (3).
  std::system(
      ( "printf '%s\\n' 'h\xC3\xA9llo w\xC3\xB6rld \xE2\x98\x83' > " + f )
          .c_str() );
  CmdResult got = run( kBin + " -m " + f );
  CmdResult exp = run( std::string( "wc -m " ) + f );
  EXPECT_EQ( got.exitCode, 0 );
  EXPECT_EQ( got.out, exp.out );
  std::system( ( "rm -f " + f ).c_str() );
}

TEST( CliWcCompat, MultipleFilesMatchWcIncludingTotal )
{
  // Differing magnitudes (one file >9 lines, one <9) prove the columns are not
  // globally aligned -- each line uses its own min-width-7, exactly like wc.
  const std::string a = "/tmp/qwc_wc_a.txt", b = "/tmp/qwc_wc_b.txt";
  std::system(
      ( "yes 'a b' | head -n 12 > " + a + " && printf '%b' 'd\\ne\\n' > " + b )
          .c_str() );
  for ( const Mode& m : kCoreModes ) {
    CmdResult got = run( kBin + " " + m.qwcFlag + " " + a + " " + b );
    CmdResult exp = run( std::string( "wc " ) + m.wcFlag + " " + a + " " + b );
    EXPECT_EQ( got.out, exp.out )
        << "qwc '" << m.qwcFlag << "' vs wc '" << m.wcFlag << "'";
  }
  std::system( ( "rm -f " + a + " " + b ).c_str() );
}
