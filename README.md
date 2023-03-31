# IoT Protocol

IoT Protocol is a protocol over TCP based on HTTP for light data traffic.

**Motivation**: HTTP 1.1 (*http://*)  protocol uses too much data traffic for IoT context. Its request minimum size is 26 bytes (https://stackoverflow.com/a/25065027/1956719) and the HOST param is mandatory for all requests. 

The IOT_PROTOCOL (*iot://*) is adapted for IoT context with light data traffic. Its request minimum size is 8 bytes withless to require HOST param for requests. 

## Preamble Version 1

```js
VERSION \n
METHOD + ID \n
PATH \n
[HEADERS \n]
[BODY_CHAR + BODY_LENGTH \n BODY]
```

### SEPARATOR char

SEPARATOR char serves to divide pieces of information

* Type: `char` | `byte` | `uint8_t`
* Size: 1 byte
* Constant: 
  * char: `\n`
  * hex: `0xA`
  * decimal: `10`
  * binary: `0b1010`

### [0] VERSION

Version is the version of iot protocol. Used for compatibility.

Format: `VERSION + SEPARATOR`. **REQUIRED** | **SINGLE**

* Type: `byte` | `uint8_t`
* Size: 1 byte
* Example: 
  * decimal: `1`
  * hex: `0x1`
  * binary: `0b00000001`

### [1] METHOD+ID

Method ID identifies the method of request and its id.

Format: `METHOD + ID + SEPARATOR`. **REQUIRED** | **SINGLE**

**METHOD**: 

Method is the reason why the request is made. **REQUIRED**

* Type: `char`. 
* Size: 1 byte
* Example: `R`

* Methods types:
  - *Signal*: method used to send signals like events
    *  char: `S`
    *  decimal: `131`
    *  hex: `0x83`
    *  binary: `0b10000011`

  - *Request*: method used to do calls that needs a response
    *  char: `R`
    *  decimal: `130`
    *  hex: `0x82`
    *  binary: `0b10000010`

  - *Response*: method used to responds a request
    *  char: `r`
    *  decimal: `114`
    *  hex: `0x72`
    *  binary: `0b1110010`

**ID**: 

Unsigned random number with up to 2^16 that identifies the request. **REQUIRED**

* Type: `uint16_t` as Big Endian format. 
* Size: 2 bytes
* Example: 
    * decimal: `276`
    * uint_8[2]: `[ 1 , 20 ]`
    * binary: `0b00000001 00010100`

### [2] PATH

The path component contains data, usually organized in hierarchical
form, that, serves to identify a resource [URI > 3.3 Path](https://www.rfc-editor.org/info/rfc3986). 

Format: `PATH + SEPARATOR`. **REQUIRED** | **SINGLE**

* Type: `string`
* Example: `/foo/bar`
* Default: `/`

### [3] HEADERS

Headers are be Key Value Pair that serves to set an attribute value for the request. Case sensitive.  

Format: `HEADER + SEPARATOR`. **OPTIONAL** | **MULTIPLE**

**HEADER**

* Type: `string`
* Format: `KEY + KEY_VALUE_SEPARATOR + VALUE`
* *KEY*: 
  * Type: `string`
* *VALUE*: 
  * Type: `string`
* *KEY_VALUE_SEPARATOR*: 
  * Constant:
    *  char: `:`
    *  decimal: `58`
    *  hex: `0x3a`
    *  binary: `0b00111010`
* Example: 
  * Single header: `foo:bar\n`
  * Multiple headers: `foo:bar\nlorem:ipsum\n`

### [4] BODY

The final data to be sent for request receiver. 

Format: `BODY_CHAR + BODY_LENGTH+SEPARATOR + BODY`. **OPTIONAL** | **SINGLE**

**BODY_CHAR**:

Identifies the body part. **REQUIRED**

  * Type: `char`
  * Size: 1 byte
  * Constant: 
    * char: `B`
    * hex: `0x42`
    * decimal: `66`

**BODY_LENGTH**: 

The body's length.  **REQUIRED**

  * Type: `uint16_t` as Big Endian format
  * Size: 2 bytes
  * Example: 
    * decimal: `2321`
    * uint_8[2]: `[ 9 , 17 ]`
    * binary: `0b00001001 00010001`

**BODY**:

The body / contents of request. **REQUIRED**

* Type: `uint8_t[]`
* Example:
  * String: `the message`
  * Buffer: `[ 116, 104, 101, 32, 109, 101, 115, 115, 97, 103, 101 ]`

## Middlewares

@TODO Explains what is a middleware and how its works

## Listen

@TODO Explains what listener method does

## Examples

@TODO List of examples on `/examples`

## References 

- `HTTP/1.1` Fielding, R., Ed., Nottingham, M., Ed., and J. Reschke, Ed., "HTTP/1.1", STD 99, RFC 9112, DOI 10.17487/RFC9112, June 2022, <https://www.rfc-editor.org/info/rfc9112>.
  
- `URI` Berners-Lee, T. Fielding, R. L. Masinter "Uniform Resource Identifier (URI): Generic Syntax" STD 66 RFC 3986 DOI 10.17487/RFC3986 <https://www.rfc-editor.org/info/rfc3986>.

