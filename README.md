# IoT Protocol

IoT Protocol is a protocol over TCP based on HTTP for light data traffic.

## Preamble Version 1

```js
VERSION\n
METHOD+ID\n
PATH\n
[HEADERS\n]
[B\n BODY]
```

### VERSION

Version is the version of iot protocol. Used for compatibility.

- Type: `byte` | `uint8_t`. **REQUIRED**
- Example: `1`

### METHOD_ID

Method ID: METHOD+ID. 

Methods: 

* Type: `char`. **REQUIRED**
* Example: `R`

- `R` | `0x82`: *Request* method used to do calls needs a response
- `S` | `0x83`: *Signal* method used to send signals like events


ID: 

Ramdom number with up to 2^16 

* Type: `uint16_t` as Big Endian format. **REQUIRED**
* Example: `1822`

### PATH

The path component contains data, usually organized in hierarchical
form, that, serves to identify a resource [URI > 3.3 Path](https://www.rfc-editor.org/info/rfc3986).

* Type: `uint8_t[]`. **REQUIRED**
* Example: 
  * `0xA` (10)
  * `/foo/bar` 
* Default: `/`

### HEADERS

Key Value Pair joined by `:` char, that, serves to set an attribute value for the request. Multiple headers must be separate para SEPARATOR char (`\n`).

* Type: `Map<string, string>`. **OPTIONAL** 
* Example: 
  * Single header: `foo:bar`
  * Multiple headers: `foo:bar\nlorem:ipsum`

### BODY

The final data to be used for request receiver. Starts with `B\n`. 

* Type: `uint8_t[]`
* Example:
  * Message: `B\nlorem ipsum message`
  * Buffer: `['B', '\n', 0x1, 0x2, 0x3, 0x4]`


## Middlewares

@TODO Explains what is a middleware and how its works

## Listen

@TODO Explains what listener method does

## Examples

@TODO List of examples on `/examples`

## References 

- `HTTP/1.1` Fielding, R., Ed., Nottingham, M., Ed., and J. Reschke, Ed., "HTTP/1.1", STD 99, RFC 9112, DOI 10.17487/RFC9112, June 2022, <https://www.rfc-editor.org/info/rfc9112>.
  
- `URI` Berners-Lee, T. Fielding, R. L. Masinter "Uniform Resource Identifier (URI): Generic Syntax" STD 66 RFC 3986 DOI 10.17487/RFC3986 <https://www.rfc-editor.org/info/rfc3986>.

