import {
  RequestType,
  URI,
  uinteger,
} from 'vscode-languageclient';


/* Pipeline-related messages */

export namespace LlvmGetPassList {
  export interface Params {
    uri: URI;
  }
  export interface Response {
    list: [string];
    descriptions: [string];
  }
  export const Type = new RequestType<Params, Response, void>('llvm/getPassList');
}

export namespace LlvmGetIRAfterPass {
  export interface Params {
    uri: URI;
    passnumber: uinteger;
  }
  export interface Response {
    uri: URI;
  }
  export const Type = new RequestType<Params, Response, void>('llvm/getIRAfterPass');
}
