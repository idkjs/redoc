
let header = {|
<html>
<head>
<title>Reason code</title>
<style>
body {
  padding: 30px;
}
.value-identifier {
    color: #000;
}

.module-identifier {
  color: #aa0;
}

.let-module-identifier,
.constructor-module-identifier,
.switch-module-identifier,
.record-module-identifier,
.field-module-identifier {
  color: #a0a;
}

.let-value-identifier,
.switch-value-identifier,
.record-value-identifier,
.field-value-identifier {
  color: #0aa;
}

.unused-identifier {
  color: #00a;
}

.declaration-var {
  color: #a50000;
}

.string {
    color: #33a20d;
}

.int {
    color: blue;
}

.boolean {
    color: red;
}

.float {
    color: orange;
}

.operator {
  color: #9b9bff;
  font-weight: bold;
}

#main {
    color: #aaa;
    white-space: pre;
    font-family: 'sf mono', monospace;
}
</style>
|};

let make = body => {
  header ++ "<pre id=\"main\">" ++ body ++ "</pre>"
};