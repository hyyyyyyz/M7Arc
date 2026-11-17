//! `m7arc` - command-line front end of the M7Arc archive engine.

mod args;
mod cmd;
mod ctx;
mod help;
mod json;
mod select;
mod term;
mod util;

use args::{parse, Opt};
use ctx::{CliError, COMMON_OPTS, EXIT_USAGE};
use help::Spec;
use m7arc_core::{CancelToken, Engine};
use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    ExitCode::from(run(args) as u8)
}

fn run(args: Vec<String>) -> i32 {
    let json = args.iter().take_while(|a| *a != "--").any(|a| a == "--json");
    let commands = cmd::all();
    let specs: Vec<&Spec> = commands.iter().map(|(spec, _)| *spec).collect();

    let Some(first) = args.first() else {
        eprint!("{}", help::overview(&specs));
        return EXIT_USAGE;
    };
    match first.as_str() {
        "-h" | "--help" => {
            print!("{}", help::overview(&specs));
            return 0;
        }
        "--version" | "-V" | "version" => {
            println!("m7arc {} (7-Zip engine {})", env!("CARGO_PKG_VERSION"), Engine::version());
            return 0;
        }
        "help" => {
            return match args.get(1) {
                None => {
                    print!("{}", help::overview(&specs));
                    0
                }
                Some(name) => match find(&commands, name) {
                    Some((spec, _)) => {
                        print!("{}", help::command_help(spec));
                        0
                    }
                    None => fail(CliError::Usage(format!("unknown command '{name}'")), json),
                },
            };
        }
        _ => {}
    }

    let Some((spec, run_command)) = find(&commands, first) else {
        return fail(CliError::Usage(format!("unknown command '{first}'")), json);
    };
    let mut tables: Vec<&[Opt]> = spec.options.to_vec();
    tables.push(COMMON_OPTS);
    let parsed = match parse(&args[1..], &tables) {
        Ok(parsed) => parsed,
        Err(e) => return fail(e.into(), json),
    };
    if parsed.flag("help") {
        print!("{}", help::command_help(spec));
        return 0;
    }
    let token = CancelToken::new();
    term::install_signal_handling(token.clone());
    match run_command(parsed, token) {
        Ok(()) => 0,
        Err(e) => fail(e, json),
    }
}

fn find<'a>(commands: &'a [(&'static Spec, cmd::RunFn)], name: &str) -> Option<&'a (&'static Spec, cmd::RunFn)> {
    commands.iter().find(|(spec, _)| spec.name == name || spec.aliases.contains(&name))
}

fn fail(error: CliError, json: bool) -> i32 {
    error.report(json);
    error.exit_code()
}
