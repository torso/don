typedef enum
{
    TYPE_BOOLEAN_TRUE,
    TYPE_BOOLEAN_FALSE,
    TYPE_INTEGER,
    TYPE_STRING,
    TYPE_STRING_WRAPPED,
    TYPE_SUBSTRING,
    TYPE_FILE,
    TYPE_ARRAY,
    TYPE_INTEGER_RANGE,
    TYPE_CONCAT_LIST,
    TYPE_FUTURE
} VType;

typedef enum
{
    TRUTHY,
    FALSY,
    FUTURE /* Value not yet known. */
} VBool;

extern VBool VGetBool(vref value);

/*
  Returns true if the value is truthy.
  Returns false if true value if falsy or not yet known.
*/
extern boolean VIsTruthy(vref value);

/*
  Returns true if the value is falsy.
  Returns false if true value if truthy or not yet known.
*/
extern boolean VIsFalsy(vref value);
