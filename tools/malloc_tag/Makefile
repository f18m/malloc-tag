EXAMPLE_JSON_FILE=../../examples/multithread/multithread_stats.json
PYTHON_MODULE_PATH=../../tools

python_tests:
	@echo "Starting Python integration TESTS (assume multithread example JSON is available)"
	$(MAKE) -s test_load_and_save_without_processing
	$(MAKE) -s test_thread_aggregation
	$(MAKE) -s test_json2dot

test_load_and_save_without_processing:
	@echo
	@echo ">> test_load_and_save_without_processing <<"
	@echo
	PYTHONPATH=$(PYTHON_MODULE_PATH):$(PYTHONPATH) \
		mtag_postprocess/postprocess.py -o /tmp/nopostprocess.json $(EXAMPLE_JSON_FILE)
	jq . /tmp/nopostprocess.json >/tmp/nopostprocess_prettyprinted.json
	jq . $(EXAMPLE_JSON_FILE) >/tmp/input_prettyprinted.json
	# give up trying to get perfectly-matching node weights between C++/Python:
	grep -v nWeightPercentage /tmp/nopostprocess_prettyprinted.json   >/tmp/nopostprocess_prettyprinted.json.2
	grep -v nWeightPercentage /tmp/input_prettyprinted.json           >/tmp/input_prettyprinted.json.2
	# check
	md5sum /tmp/input_prettyprinted.json.2 /tmp/nopostprocess_prettyprinted.json.2
	@cmp --silent /tmp/input_prettyprinted.json.2 /tmp/nopostprocess_prettyprinted.json.2 || ( \
		echo; \
		echo "!! Failed test; the two files are different !!" ; \
		diff -bU3 /tmp/input_prettyprinted.json /tmp/nopostprocess_prettyprinted.json ; \
		echo; \
		exit 2 \
	)
	rm -f /tmp/nopostprocess_prettyprinted.json /tmp/input_prettyprinted.json

test_thread_aggregation:
	@echo
	@echo ">> test_thread_aggregation <<"
	@echo
	PYTHONPATH=$(PYTHON_MODULE_PATH):$(PYTHONPATH) \
		mtag_postprocess/postprocess.py -o /tmp/threadagg.json -c mtag_postprocess/example_agg_rules.json $(EXAMPLE_JSON_FILE)
	jq . /tmp/threadagg.json >/tmp/nopostprocess_prettyprinted.json
	jq . $(EXAMPLE_JSON_FILE) >/tmp/input_prettyprinted.json
	#
	# FIXME: write an integration test checking that nBytesSelfAllocated of
	#          ExampleThr/0,ExampleThr/1 
	#        is matching the sum of 
	#          ExampleThr/0   and   ExampleThr/1
	#

test_json2dot:
	@echo
	@echo ">> test_json2dot <<"
	@echo
	# test creation of .dot output file:
	PYTHONPATH=$(PYTHON_MODULE_PATH):$(PYTHONPATH) \
		mtag_json2dot/json2dot.py -o /tmp/testoutput.dot $(EXAMPLE_JSON_FILE)
	# test creation of .svg output file:
	PYTHONPATH=$(PYTHON_MODULE_PATH):$(PYTHONPATH) \
		mtag_json2dot/json2dot.py -o /tmp/testoutput.svg $(EXAMPLE_JSON_FILE)
