// Copyright & License details are available under JXCORE_LICENSE file

var path = require("path");
var fs = require("fs");
var cp = require("child_process");
var prepare_packages = require("./prepare_packages");
var jx = require("jxtools");

var flags_id = process.argv.indexOf("-flags");
var flags = "_";
if (flags_id !== -1) {
  flags = process.argv[flags_id + 1];
  if (!flags) {
    flags = "";
    process.argv.splice(flags_id, 1);
  } else {
    process.argv.splice(flags_id, 2);
  }
}

var test_all = process.argv.indexOf("-a") !== -1 || flags.indexOf("a") !== -1;
var test_packages = process.argv.indexOf("-p") !== -1
  || flags.indexOf("p") !== -1 || test_all;
var test_natives = process.argv.indexOf("-n") !== -1
  || flags.indexOf("n") !== -1 || test_all;
var test_js = process.argv.indexOf("-j") !== -1 || flags.indexOf("j") !== -1
  || (!test_packages && !test_natives) || test_all;

var silent = process.argv.indexOf("-s") !== -1 || flags.indexOf("s") !== -1;
var no_cleanup = process.argv.indexOf("-nc") !== -1;
var help = process.argv.indexOf("--help") !== -1;

prepare_packages.silent = silent;
prepare_packages.force_refresh = process.argv.indexOf("-f") !== -1;

if (help) {
  console.log("Usage:\n");
  console.log("\trun.js [-j | -p | -n | -a | -v | -f] [test_folder1 test_folder2 ...] -file test_folder/test_file.js \n");
  console.log("\t-j     tests only .js test cases. Default when omitted");
  console.log("\t-p     tests only packaged test cases");
  console.log("\t-n     tests only native packaged test cases");
  console.log("\t-a     tests all: -j, -p, -n");
  console.log("\t-s     silent: hides extra messages");
  console.log("\t-f     packages are created once per jx version. Use -f to force refresh them");
  console.log("\t-file  allows to execute test just for one js file");
  console.log("");
  console.log("\texamples:");
  console.log("\t\trun.js                 - runs .js tests from all subfolders of 'test'");
  console.log("\t\trun.js jxcore          - runs .js tests for 'jxcore'");
  console.log("\t\trun.js jxcore simple   - runs .js tests for 'jxcore' and 'simple'");
  console.log("\t\trun.js -p jxcore       - compiles all ./test/jxcore/*.js files into .jx packages\n"
    + "\t\t\t\t\t('jxcore_package' output folder) and runs them");
  console.log("\t\trun.js -n simple       - compiles all ./test/simple/*.js files into native packages\n"
    + "\t\t\t\t\t('simple_package' output folder) and runs them");
  console.log("");

  return;
}

var single_test_dir = null;

/**
 * Checks -file argument (when script was called like: run.js -file
 * jxcore/test-http-create.js)
 */
var checkFile = function () {
  var _id = process.argv.indexOf("-file");
  if (_id !== -1) {
    if (!process.argv[_id + 1]) {
      jxcore.utils.console.log(
        "JS test file not provided after -file arg. Skipping.", "red");
      process.exit();
    }

    var js_file = path.join(process.cwd(), process.argv[_id + 1]);
    if (!fs.existsSync(js_file)) {
      jxcore.utils.console.log(
        "JS test file provided after -file does not exists. Skipping.",
        "red");
      process.exit();
    }

    process.argv.splice(_id, 2);

    var basedirname = path.basename(path.dirname(js_file));
    single_test_dir = {
      name: prepare_packages
        .getOutputDirBasename(basedirname + "_single", null)
    };

    // preparing separate folder for test file
    var testDir = path.join(__dirname, single_test_dir.name);
    jx.rmdirSync(testDir);
    single_test_dir.native_dir = path.join(path.dirname(testDir),
      prepare_packages.getOutputDirBasename(single_test_dir.name, true));
    if (test_natives) jx.rmdirSync(single_test_dir.native_dir);
    single_test_dir.package_dir = path.join(path.dirname(testDir),
      prepare_packages.getOutputDirBasename(single_test_dir.name, false));
    if (test_packages) jx.rmdirSync(single_test_dir.package_dir);
    fs.mkdirSync(testDir);

    jx.copyFileSync(js_file, path.join(testDir, path.basename(js_file)));
    jx.copyFileSync(js_file + ".json", path.join(testDir, path
      .basename(js_file)
    + ".json"));
    jx.copyFileSync(js_file + ".jxcore.config", path.join(testDir, path
      .basename(js_file)
    + ".jxcore.config"));
    jx.copyFileSync(js_file.replace(".js", ".out"), path.join(testDir, path
      .basename(js_file, ".js")
    + ".out"));
    jx.copyFileSync(js_file.replace(".js", ".jxp"), path.join(testDir, path
      .basename(js_file, ".js")
    + ".jxp"));
    prepare_packages.checkJSON(js_file, testDir);

    prepare_packages.renderTestCfg(testDir);
    // console.log("single_test_dir", single_test_dir);
  }
}();

var folders = [];

// stripping everything except args like "jxcore" "simple" etc - test folder
// names
var _arr = process.argv.slice(1).join("|").replace(__filename, "").replace(
  process.execPath, "").trim().split("|");
var dirs = [];
for (var o in _arr)
  if (_arr[o] && _arr[o].slice(0, 1) != "-") dirs.push(_arr[o]);

if (single_test_dir) dirs.push(single_test_dir.name);

/**
 * @param what -
 *          "-j", "-n", "-p"
 * @param cb
 */
var run = function (what, cb) {

  var args = ["tools/test.py", "-p", "color"];

  for (var o in dirs) {

    if (what === "-j")
      args.push(dirs[o]);
    else
      args.push(prepare_packages.getOutputDirBasename(dirs[o], what == "-n"));
  }

  prepare_packages.processFolders(dirs, what == "-p", what == "-n");

  var info = "plain js files";
  if (what == "-n") info = "native packages";
  if (what == "-p") info = "jx packages";
  jxcore.utils.console.log("Launching tests for", info, ":", "green");

  if (!silent) console.log('Command: python', args.join(" "));

  var child = cp.spawn("python", args);

  child.on('close', cb);
  child.stdout.pipe(process.stdout);
  child.stderr.pipe(process.stderr);
};

var runNext = function () {

  console.log("");
  var what = arr.shift();
  if (what)
    run(what, runNext);
  else
    finish();
};

var finish = function () {
  console.log("");
};

if (process.argv.indexOf("--render-test-configs") !== -1) {
  // takes jxcore/testcfg.py and based on it renders it's versions for : gc,
  // internet, pummel, simple
  prepare_packages.renderTestCfg(path.join(__dirname, "gc"), "GC");
  prepare_packages.renderTestCfg(path.join(__dirname, "internet"), "Internet");
  prepare_packages.renderTestCfg(path.join(__dirname, "pummel"), "Pummel");
  prepare_packages.renderTestCfg(path.join(__dirname, "simple"), "Simple");
  prepare_packages.renderTestCfg(path.join(__dirname, "jxcore-npm"),
    "JXcoreNPM");

  console.log("ok");
  return;
}

var arr = [];
if (test_js) arr.push("-j");
if (test_packages) arr.push("-p");
if (test_natives) arr.push("-n");

runNext();
