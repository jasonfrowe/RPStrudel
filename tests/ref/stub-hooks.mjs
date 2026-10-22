// @strudel/core's bundle imports SalatRepl from @kabelsalat/web for its REPL,
// which the published package doesn't export. The pattern engine never uses
// it, so hand Node an empty stand-in.
export async function resolve(specifier, context, next) {
  if (specifier === '@kabelsalat/web') {
    return { url: 'data:text/javascript,export class SalatRepl {}', shortCircuit: true };
  }
  return next(specifier, context);
}
