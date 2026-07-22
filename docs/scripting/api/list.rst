Lists
=====

This page documents the ``List`` type. ``List`` is :ref:`clonable <clonability>`.

General concepts
----------------

A list is an ordered collection of values, which may be of different types. Elements in a list can be added and removed, and the list grows dynamically to accomodate
new incoming elements. Indices in a list start at 1 and can be negative: -1 represents the last element, -2 the second-to-last element, and so on.

Lists can be created using a list literal, and elements can be accessed and modified using the indexing operator ``[]``:

.. code:: phon

    var lst = ["A", 111, "hello", 3.14]
    lst[-1] = "pi"
    print(lst) # prints "[A, 111, hello, pi]"

Lists can be concatenated using the ``+`` operator:

.. code:: phon

    var x = [1, 2]
    var y = [3, 4]
    print(x + y) # prints "[1, 2, 3, 4]"

Lists can be iterated with a ``for`` loop:

.. code:: phon

    for value in ["a", "b", "c"] do
        print(value)
    end

Functions that modify a list in place take it as a ``ref`` parameter: the
argument must be a variable (or an element or field), and the change is
visible at the call site.


Functions
---------

.. function:: append(ref list as List, item as Object)

Inserts ``item`` at the end of ``list``.

.. code:: phon

    var lst = [1, 2]
    append(lst, 3)
    print(lst) # prints "[1, 2, 3]"

See also: :func:`prepend`, :func:`insert`

------------

.. function:: clear(ref list as List)

Empties the content of the list. After this function is called, a call to :func:`is_empty` will return ``true``.

------------

.. function:: contains(list as List, item as Object)

Returns ``true`` if ``item`` is in the list and ``false`` otherwise.

------------

.. function:: find(list as List, item as Object)


Returns the index of ``item`` in the list. If the element is not found, 0 is returned.

Note: If the list is sorted, you can use :func:`sorted_find` instead, which is a little faster since it can take advantage of the fact
that the order of the elements is known.

See also: :func:`find_back`, :func:`sorted_find`

------------

.. function:: find(list as List, item as Object, pos as Integer)


Returns the index of ``item`` in the list, starting the search at index ``pos``. If the element is not found, 0 is returned.

See also: :func:`find_back`, :func:`sorted_find`

------------


.. function:: find_back(list as List, item as Object)


Returns the index of the *last* occurrence of ``item`` in the list, searching from the end. If the element is not found,
0 is returned.

.. code:: phon

    assert(find_back([10, 20, 10], 10) == 3)

See also: :func:`find`

------------

.. function:: first(list as List)

Returns the first item in the list. Calling this function on an empty list
raises an error.

See also: :func:`last`

------------

.. function:: insert(ref list as List, pos as Integer, item as Object)

Inserts the element ``item`` at index ``pos``. If ``pos`` is past the end of
the list, ``item`` is appended.

See also: :func:`sorted_insert`


------------

.. function:: intersect(list1 as List, list2 as List)

Returns a new list which contains all the elements that are in both ``list1`` and ``list2``.
The lists don't need to be sorted; the result preserves the order of ``list1`` and contains no duplicates.

.. code:: phon

    print(intersect([3, 1, 2], [2, 3, 5])) # prints "[3, 2]"

See also: :func:`unite`, :func:`subtract`

------------

.. function:: is_empty(list as List)

Returns ``true`` if the list doesn't contain any element, and ``false`` if it does.

------------

.. function:: is_sorted(list as List)

Returns ``true`` if all the elements are sorted in ascending order.

See also: :func:`sort`

------------

.. function:: join(items as List, delim as String)

Returns a string in which all the elements in ``items`` have been joined with the separator ``delim``.

.. code:: phon

    assert(join(["a", "b", "c"], "-") == "a-b-c")

------------

.. function:: last(list as List)

Returns the last item in the list. Calling this function on an empty list
raises an error.

See also: :func:`first`

------------

.. function:: left(list as List, n as Integer)

Returns a new list containing the ``n`` first elements of the list (or a copy
of the whole list if it has fewer than ``n`` elements).

See also: :func:`right`

------------

.. function:: len(list as List)

Returns the number of elements in the list.

------------

.. function:: parallel_map(list as List, fn as Function)

Applies the function ``fn`` to each element of the list on the runtime's thread
pool, and returns a new list containing the results in order. ``fn`` must be a
top-level function or a non-capturing lambda.

.. code:: phon

    print(parallel_map([1, 2, 3], x -> x * 2)) # prints "[2, 4, 6]"

------------

.. function:: pop(ref list as List)

Removes the last element from the list and returns it. Calling this function
on an empty list raises an error.

See also: :func:`shift`

------------

.. function:: prepend(ref list as List, item as Object)

Inserts ``item`` at the beginning of the list.

See also: :func:`append`, :func:`insert`


------------

.. function:: remove(ref list as List, item as Object)

Removes all the elements in the list that are equal to ``item``.


See also: :func:`remove_at`, :func:`remove_first`, :func:`remove_last`


------------

.. function:: remove_first(ref list as List, item as Object)

Removes the first element in the list that is equal to ``item``.


See also: :func:`remove_at`, :func:`remove`, :func:`remove_last`

------------

.. function:: remove_last(ref list as List, item as Object)

Removes the last element in the list that is equal to ``item``.


See also: :func:`remove_at`, :func:`remove`, :func:`remove_first`

------------

.. function:: remove_at(ref list as List, pos as Integer)

Removes the element at index ``pos``.


See also: :func:`remove`, :func:`remove_first`, :func:`remove_last`

------------


.. function:: reverse(ref list as List)

Reverses the order of the elements in the list.

.. code:: phon

    var lst = [1, 2, 3]
    reverse(lst)
    print(lst) # prints "[3, 2, 1]"

See also: :func:`sort`

------------

.. function:: right(list as List, n as Integer)

Returns a new list containing the ``n`` last elements of the list (or a copy
of the whole list if it has fewer than ``n`` elements).

See also: :func:`left`

------------

.. function:: sample(list as List, n as Integer)

Returns a list containing ``n`` elements from the list drawn at random,
without replacement.

------------

.. function:: shift(ref list as List)

Removes the first element from the list and returns it. Calling this function
on an empty list raises an error.

See also: :func:`pop`

------------

.. function:: shuffle(ref list as List)

Randomizes the order of the elements in the list.

------------

.. function:: sort(ref list as List)

Sorts the elements in the list in increasing order. The elements should be of the same type.

.. code:: phon

    var lst = [3, 1, 2]
    sort(lst)
    print(lst) # prints "[1, 2, 3]"

See also: :func:`is_sorted`, :func:`reverse`

------------

.. function:: sorted_find(list as List, item as Object)

Finds the index of ``item`` in a sorted list. **If** ``item`` **is not in the
list, 0 is returned.** Note that if the list is not sorted, the result of this
operation is undefined.

This function is generally faster than :func:`find` for sorted lists, as it takes logarithmic (as opposed to linear) time on average.

.. code:: phon

    assert(sorted_find([10, 20, 30], 20) == 2)
    assert(sorted_find([10, 20, 30], 25) == 0)

See also: :func:`find`

------------

.. function:: sorted_insert(ref list as List, item as Object)

Inserts ``item`` before the first element that is not less than ``item``, so
that a sorted list stays sorted. If the list is not sorted, the result of this
operation is undefined.

See also: :func:`insert`

------------

.. function:: subtract(list1 as List, list2 as List)

Returns a new list which contains all the elements that are in ``list1`` but not in ``list2``.
The lists don't need to be sorted; the result preserves the order of ``list1`` and contains no duplicates.

See also: :func:`intersect`, :func:`unite`

------------

.. function:: to_string(list as List)

Returns a string representation of the list.

.. code:: phon

    assert(to_string([1, 2]) == "[1, 2]")

------------

.. function:: unite(list1 as List, list2 as List)

Returns a new list which contains all the elements that are in ``list1`` and/or in ``list2``.
The lists don't need to be sorted; the result preserves the order of the inputs and contains no duplicates.

.. code:: phon

    print(unite([1, 2, 3], [3, 4])) # prints "[1, 2, 3, 4]"

See also: :func:`intersect`, :func:`subtract`
