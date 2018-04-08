
let codeBlockPrefix = "DOCRE_CODE_BLOCK_";

let (/+) = Filename.concat;

type codeContext = Normal | Node | Window | Iframe | Canvas | Div | Log;
let contextString = c => switch c {
| Normal => "normal"
| Log => "log"
| Node => "node"
| Window => "window"
| Iframe => "iframe"
| Canvas => "canvas"
| Div => "div"
};

type codeOptions = {
  context: codeContext,
  shouldParseFail: bool,
  shouldTypeFail: bool,
  shouldRaise: bool,
  dontRun: bool,
  isolate: bool,
  sharedAs: option(string),
  uses: list(string),
  hide: bool,
};

let matchOption = (text, option) => if (Str.string_match(Str.regexp("^" ++ option ++ "(\([^)]+\))$"), text, 0)) {
  Some(Str.matched_group(1, text));
} else {
  None
};

let parseCodeOptions = lang => {
  let parts = Str.split(Str.regexp_string(";"), lang);
  if (List.mem("skip", parts)
  || List.mem("bash", parts)
  || List.mem("txt", parts)
  || List.mem("js", parts)
  || List.mem("javascript", parts)
  || List.mem("sh", parts)) {
    None
  } else {
    Some(List.fold_left((options, item) => {
      switch item {
      | "window" => {...options, context: Window}
      | "canvas" => {...options, context: Canvas}
      | "iframe" => {...options, context: Iframe}
      | "log" => {...options, context: Log}
      | "div" => {...options, context: Div}

      | "raises" => {...options, shouldRaise: true}
      | "parse-fail" => {...options, shouldParseFail: true}
      | "type-fail" => {...options, shouldTypeFail: true}
      | "isolate" => {...options, isolate: true}
      | "dont-run" => {...options, dontRun: true}
      | "hide" => {...options, hide: true}
      | "reason" => options
      | _ => {
        switch (matchOption(item, "shared")) {
        | Some(name) => {...options, sharedAs: Some(name)}
        | None => switch (matchOption(item, "use")) {
        | Some(name) => {...options, uses: [name, ...options.uses]}
        | None => {

          print_endline("Skipping unexpected code option: " ++ item);
          options

        }
        }
        }
      }
      }
    }, {
      context: Normal,
      shouldParseFail: false,
      shouldTypeFail: false,
      shouldRaise: false,
      dontRun: false,
      isolate: false,
      sharedAs: None,
      uses: [],
      hide: false,
    }, parts))
  }
};

type codeBlock = {
  el: Omd.element,
  id: int,
  fileName: string,
  options: codeOptions,
  content: string,
};

type compilationStatus =
  /* | Skipped */
  | ParseError(string)
  | TypeError(string, string) /* error & cmt file */
  | Success(string, string) /* cmt & js files */
  ; /* TODO maybe do the bundling earlier, so that it can be processed in parallel? */

type compiledBlock = {
  block: codeBlock,
  status: compilationStatus,
  /* reasonSource: string, */
};

let sprintf = Printf.sprintf;
let html = Omd_utils.htmlentities;
let escapeScript = text => Str.global_replace(Str.regexp_string("</script"), "<\\/script", text);

let highlight = ({id, content, options}, status, bundle) => {
  let cmt = switch status {
  | ParseError(_) => None
  | TypeError(_, cmt) => Some(cmt)
  | Success(cmt, _) => Some(cmt)
  };
  let code = switch cmt {
  | None => html(content)
  | Some(cmt) => CodeHighlight.highlight(content, cmt)
  };

  let after = switch status {
  | ParseError(text) => sprintf({|<div class='parse-error'>Parse Error:\n%s</div>|}, html(text))
  | TypeError(text, _) => sprintf({|<div class='type-error'>Type Error:\n%s</div>|}, html(text))
  | Success(cmt, js) => Printf.sprintf({|%s<script type='docre-bundle' data-block-id='%d'>%s</script>|},
      switch options.context {
      | Node => ""
      | _ => sprintf({|<div data-block-id='%d' data-context=%S class='block-target'></div>|}, id, contextString(options.context))
      },
      id,
      bundle(js)
    )
  };

  sprintf(
    {|<div class='code-block'>
  <pre class='code' data-block-id='%d' id='block-%d'><code>%s</code></pre>
  <script type='docre-source' data-block-id="%d">%s</script>
  %s
</div>|},
    id,
    id,
    code,
    id,
    escapeScript(content),
    after
  )
};

let splitLines = text => Str.split(Str.regexp_string("\n"), text);

let sliceToEnd = (text, i) => String.sub(text, i, String.length(text) - i);

/* let removeHashes = text => Str.global_replace(Str.regexp("^#"), " ", text); */
let removeHashes = text => {
  let rec loop = lines => switch lines {
  | [] => []
  | ["", ...rest] => ["", ...loop(rest)]
  | [one, ...rest] when one.[0] == '#' => [" " ++ sliceToEnd(one, 1), ...loop(rest)]
  | _ => lines
  };
  let front = loop(splitLines(text));
  String.concat("\n", List.rev(loop(List.rev(front))))
};


let hashAll = text => splitLines(text) |> List.map(t => (t == "" || t.[0] != '#') ? "#" ++ t : t) |> String.concat("\n");

let getCodeBlocks = (markdowns, cmts) => {
  let codeBlocks = ref((0, []));
  let shared = ref([]);
  let addBlock = (el, fileName, lang, content) => {
    let options = parseCodeOptions(lang);
    switch (options) {
    | None => ()
    | Some(options) => {
      let content = List.fold_left((content, name) => {
        let text = List.assoc(name, shared^) |> hashAll;
        let rx = Str.regexp_string("#%{code}%");
        switch (Str.search_forward(rx, text, 0)) {
        | exception Not_found => text ++ "\n" ++ content
        | _ => Str.replace_first(rx, content, text)
        }
      }, content, options.uses);
      /* let content = switch (options.uses) {
      | [] => content
      | uses => (List.map(name => List.assoc(name, shared^) |> hashAll, uses) @ [content]) |> String.concat("\n")
      }; */
      /* TODO I might want to be able to have something be shared & visible... not sure though */
      switch (options.sharedAs) {
      | None => {
        let (id, blocks) = codeBlocks^;
        codeBlocks := (id + 1, [{el, id, fileName, options, content}, ...blocks]);
      }
      | Some(name) => shared := [(name, content), ...shared^];
      };
    }
    }
  };

  let collect = (fileName, md) => Omd.Representation.visit(el => switch el {
    | Omd.Code_block(lang, contents) => {
      addBlock(el, fileName, lang, contents);
      None
    }
    | _ => None
  }, md) |> ignore;

  markdowns |> List.iter(((path, contents, name)) => {
    collect(path, contents);
  });

  cmts |> List.iter(((name, cmt, _, topDoc, allDocs)) => {
    Infix.(topDoc |?>> collect(cmt) |> ignore);
    allDocs |> List.iter(PrepareDocs.iter(((name, docString, _)) => {
      switch docString {
      | None => ()
      | Some(docString) => collect(cmt ++ " > " ++ name, docString)
      }
    }))
  });

  codeBlocks^ |> snd
};

open Infix;

let refmtCommand = (base, re, refmt) => {
  Printf.sprintf({|cat %s | %s --print binary > %s.ast && %s %s.ast %s_ppx.ast|},
  re,
  refmt,
  re,
  base /+ "node_modules/bs-platform/lib/reactjs_jsx_ppx_2.exe",
  re,
  re
  )
};

let justBscCommand = (base, re, includes) => {
  Printf.sprintf(
    {|%s -w -A %s -impl %s|},
    base /+ "node_modules/.bin/bsc",
    includes |> List.map(Printf.sprintf("-I %S")) |> String.concat(" "),
    re
  )
};

let snippetLoader = (name, basePath, snippetPath) => Printf.sprintf({|
var path = require('path');
var Module = require('module');
var oldResolveFilename = Module._resolveFilename;
Module._resolveFilename = function (request, parent, isMain) {
  var name = '%s/';
  var base = '%s';
  if (request.indexOf(name) === 0) {
    return oldResolveFilename.call(this, path.join(base, request.slice(name.length)), parent, isMain);
  }

  return oldResolveFilename.call(this, request, parent, isMain);
};
require('%s')
|}, name, basePath, snippetPath);

let compileSnippets = (base, blocks) => {
  let blocksByEl = Hashtbl.create(100);

  let config = Json.parse(Files.readFile(base /+ "bsconfig.json") |! "No bsconfig.json found");
  let isNative = config |> Json.get("entries") != None;

  let deps = [base /+ (isNative ? "lib/bs/js/src" : "lib/bs/src")]; /* TODO find dependency build directories */

  let tmp = base /+ "node_modules/.docre";
  Files.mkdirp(tmp);

  let blockFileName = id => codeBlockPrefix ++ string_of_int(id);

  let blocks = blocks |> List.map(({el, id, fileName, options, content} as block) => {

    let name = blockFileName(id);
    let re = tmp /+ name ++ ".re";
    let cmt = tmp /+ name ++ ".re_ppx.cmt";
    let js = tmp /+ name ++ ".re_ppx.js";

    /* let cmt = base /+ "lib/bs/src/" ++ name ++ ".cmt";
    let js = base /+ "lib/js/src/" ++ name ++ ".js"; */
    let reasonContent = removeHashes(content) ++ " /* " ++ fileName ++ " */";
    /* How do we knock the cache based on dependencies mtimes? */
    /* Maybe find all the .cmj files in the deps, and find the most recent one? */
    /* And then it's ok to take a bit of time I guess */
    /* TODO don't do extra work if nothing has changed. */
    /* if (Files.readFile(re) == Some(reasonContent)) {
      /* ok we're done */
    } else {

    } */

    Files.writeFile(re, reasonContent) |> ignore;

    let refmt = base /+ "node_modules/bs-platform/lib/refmt3.exe";
    let refmt = if (Files.exists(refmt)) {
      refmt
    } else {
      base /+ "node_modules/bs-platform/lib/refmt.exe";
    };

    let cmd = refmtCommand(base, re, refmt);
    let (output, err, success) = Commands.execFull(cmd);
    let status = if (!success) {
      /* TODO exit hard if it parse fails and you didn't mean to or seomthing. Report this at the end. */
      let out = String.concat("\n", output) ++ String.concat("\n", err);
      let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
      if (!options.shouldParseFail) {
        print_endline("Failed to parse " ++ re);
        print_endline(out);
        print_endline(reasonContent);
        print_newline();
      };
      ParseError(out);
    } else {
      let cmd = justBscCommand(base, re ++ "_ppx.ast", deps);
      let (output, err, success) = Commands.execFull(cmd);
      if (!success) {
        /* TODO exit hard if it parse fails and you didn't mean to or seomthing. Report this at the end. */
        let out = String.concat("\n", output) ++ String.concat("\n", err);
        let out = Str.global_replace(Str.regexp_string(re), "<snippet>", out);
        if (!options.shouldTypeFail) {
          print_endline("Failed to compile " ++ re);
          print_endline(out);
          print_endline(reasonContent);
          print_newline();
        };
        TypeError(out, cmt);
      } else { Success(cmt, js) };
    };

    Hashtbl.replace(blocksByEl, el, {status, block});

    {status, block}
  });

  /* let (output, err, success) = Commands.execFull(base /+ "node_modules/.bin/bsb" ++ " -make-world"); */
  /* let (output, err, success) = Commands.execFull(base /+ "node_modules/.bin/bsb" ++ " -make-world -backend js");
  if (!success) {
    print_endline("Bsb output:");
    print_endline(String.concat("\n", output));
    print_endline("Error while running bsb on examples");
  }; */
      /* Unix.unlink(src /+ name ++ ".re"); */

  (blocksByEl, blocks)
};

let escape = text => text
|> Str.global_replace(Str.regexp_string("\\"), "\\\\")
|> Str.global_replace(Str.regexp_string("\n"), " ")
|> Str.global_replace(Str.regexp_string("\""), "\\\"")
;

/* TODO allow package-global settings, like "run this in node" */
let process = (~test, markdowns, cmts, base, dest) =>  {

  let blocks = getCodeBlocks(markdowns, cmts);
  let packageJson = Json.parse(Files.readFile(base /+ "package.json") |! "No package.json in " ++ base);
  let packageJsonName = packageJson |> Json.get("name") |?> Json.string |! "Missing name in package.json";

  let (blocksByEl, blocks) = compileSnippets(base, blocks);

  if (test) {
    print_endline("Running tests");

    /* TODO run in parallel - maybe all in the same node process?? */
    blocks |> List.iter(({status, block: {options, fileName, id}}) => {
      if (test && !options.dontRun && !options.shouldParseFail && !options.shouldTypeFail && options.context == Normal) {
        print_endline(string_of_int(id) ++ " - " ++ fileName);
        switch status {
        | Success(_, js) =>
        let cmd = Printf.sprintf("node -e \"%s\"", snippetLoader(packageJsonName, Files.absify(base), js) |> escape);
        let (output, err, success) = Commands.execFull(cmd);
        /* let (output, err, success) = Commands.execFull("node " ++ js ++ ""); */
        if (options.shouldRaise) {
          if (success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Expected to fail but didnt " ++ string_of_int(id) ++ " in " ++ fileName);
          }
        } else {
          if (!success) {
            print_endline(String.concat("\n", output));
            print_endline(String.concat("\n", err));
            print_endline("Failed to run " ++ string_of_int(id) ++ " in " ++ fileName);
          };
        }
        | _ => print_endline("Not running because of error")
        }
      };
      /* let jsContents = Files.readFile(js); */
      ()
    });
  };

  let optMap = (fn, items) => List.fold_left((result, item) => switch (fn(item)) { | None => result | Some(res) => [res, ...result]}, [], items);

  let allJsFiles: list(string) = blocks |> optMap(({status}) => switch status {
    | Success(_, js) => Some(js) | _ => None
  });

  let bundle = Packre.Pack.process(~mode=Packre.Types.JustExternals, ~renames=[(packageJsonName, base)], ~base, allJsFiles);
  Files.writeFile(dest /+ "all-deps.js", bundle ++ ";window.loadedAllDeps = true;") |> ignore;

  (element) => switch element {
  | Omd.Code_block(lang, content) => {
    switch (Hashtbl.find(blocksByEl, element)) {
    | exception Not_found => {
      let options = parseCodeOptions(lang);
      switch options {
      | Some({sharedAs: Some(_)}) => Some("")
      | _ => None
      }
    }
    | {block, status} => {
      if (block.options.hide) {
        Some("")
      } else {
        Some(highlight(block, status, js => Packre.Pack.process(~mode=Packre.Types.ExternalEverything, ~renames=[(packageJsonName, base)], ~base, [js])))
      }
    }
    }
  }
  | _ => None
  };
};