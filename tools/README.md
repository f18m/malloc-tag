# malloc-tag tools

This README describes the Python-based tools to postprocess malloc-tag snapshots:

* mtag-postprocess
* mtag-json2dot

## How to install

Installing these utilities from [pypi](https://pypi.org/project/malloctag-tools/) is really simple:

```
pip3 install -upgrade malloctag-tools
```

## Postprocess 

It might happen that inside your C/C++ application you have several threads running the same code.
In this case the pure snapshot provided by the malloc-tag framework (where 1 thread means 1 tree) can be hard to read/follow.
In such cases you might want to aggregate together all memory operations done by hese identical threads.

The tool `mtag-postprocess` makes this very simple. An example usage follows.

Let's assume you need to aggregate the memory alloc/frees done by all threads having the prefix `MyThreadPrefix`.
First of all create a suitable config `agg_config.json`:

```
{
    "comment": "This is a simple example of postprocessing rule",
    "rule0": {
        "aggregate_trees": {
            "matching_prefix": "MyThreadPrefix/*"
        }
    }
}
```

Then launch the postprocessing utility:

```
mtag-postprocess  --output post-process-out.json --config agg_config.json    <malloc-tag-snapshot-file.json>
```

The output file is a JSON file with the same identical format used by malloc-tag C++ library.
This allows to easily chain and combine different post-processing steps.


## Rendering

Once you're done with post-processing of the malloc-tag JSON snapshot, you might want to inspect it visually.
This can be done thanks to Graphviz:

```
mtag-json2dot --output nice-picture.svg  <malloc-tag-snapshot-file.json>
```

Check the top-level README for example of generated SVG output.
