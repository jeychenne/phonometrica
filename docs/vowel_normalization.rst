.. _vowel-normalization:

Vowel normalization
===================

Vowel normalization is a standard procedure in sociophonetic research that aims to reduce the effects of
physiological differences between speakers (such as vocal tract length) while preserving sociolinguistic and
phonological variation. Phonometrica provides four widely used normalization methods, accessible from both
the :ref:`dataset view <dataset-view>` and the :ref:`concordance view <concordance-view>`.


Using vowel normalization
-------------------------

To normalize formant data, click the |normvowel| **Normalize vowels** button in the toolbar of a dataset or
concordance view. This opens a dialog where you can configure the normalization:

- **Method**: select one of the four available methods (see below).
- **Formant column(s)**: select one or more numeric columns containing formant measurements (e.g. F1, F2, F3). Most methods accept any number of formant columns; Watt & Fabricius requires exactly two (F1 and F2).
- **Speaker column**: select a text column that identifies the speaker for each observation. All methods compute per-speaker statistics.
- **Suffix**: the suffix appended to each formant column name to form the output column name (e.g. ``F1_lob``, ``F2_lob``). This is set automatically based on the selected method, but can be edited.

The normalized values are added as new columns at the end of the table. The original columns are not modified.


Point vowel mapping (Watt & Fabricius)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the **Watt & Fabricius** method is selected, an additional panel appears:

- **Vowel column**: select a text column that contains a vowel label for each observation (e.g. a phonemic transcription).
- **Label for /i/**, **Label for /a/**, **Label for /u/**: specify which labels in the vowel column correspond to the three point vowels used to compute the centroid. The dialog attempts to auto-select common IPA symbols, but you should verify that the mapping is correct for your transcription conventions.


Available methods
-----------------

Lobanov (1971)
~~~~~~~~~~~~~~

The Lobanov method applies a z-score transformation to each formant independently, within each speaker:

.. math::

   F_n' = \frac{F_n - \mu_n}{\sigma_n}

where :math:`\mu_n` and :math:`\sigma_n` are the mean and standard deviation of formant :math:`F_n` for the
speaker.

This is the most commonly used vowel normalization method. It is vowel-extrinsic (it uses the distribution of
all vowels for a given speaker) and speaker-intrinsic (each speaker is normalized independently). The output
values are dimensionless z-scores centered around 0.


Nearey 1 (1978) — per-formant
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nearey's first method (also called *individual log-mean* or *per-formant extrinsic*) subtracts the speaker's
mean log-formant from each log-transformed formant value, independently for each formant:

.. math::

   F_n' = \ln(F_n) - \overline{\ln(F_n)}

where the mean is computed over all observations of formant :math:`F_n` for the speaker.

This method operates in the log-frequency domain, which is motivated by the observation that formant
frequencies are approximately log-normally distributed. Like Lobanov, it is vowel-extrinsic and
speaker-intrinsic, but it preserves the log-scale relationship between formants.


Nearey 2 (1978) — uniform
~~~~~~~~~~~~~~~~~~~~~~~~~~

Nearey's second method (also called *uniform* or *grand log-mean*) subtracts a single grand mean of all
log-formant values from each log-transformed value:

.. math::

   F_n' = \ln(F_n) - \overline{\ln(F)}

where :math:`\overline{\ln(F)}` is the grand mean of all log-formant values (across all formants) for the
speaker.

The key difference from Nearey 1 is that a single correction factor is applied uniformly across all formants
for a given speaker, rather than a separate correction for each formant. This assumes that a single
vocal-tract-length factor affects all formants equally.


Watt & Fabricius (2002)
~~~~~~~~~~~~~~~~~~~~~~~

The Watt & Fabricius method normalizes formant values relative to a speaker-specific centroid computed from
the three point vowels /i/, /a/, and /u/:

.. math::

   F_n' = \frac{F_n - S_n}{S_n}

where :math:`S_n` is the centroid coordinate for formant :math:`n`, computed as the mean of the point vowel
values:

.. math::

   S_n = \frac{F_n(i) + F_n(a) + F_n(u')}{3}

Following Watt & Fabricius (2002), F1 of /u/ is not measured directly but is estimated as
:math:`F_1(u') = F_1(i)`. This avoids problems caused by variation in the realization of /u/ across speakers
and dialects.

This method requires exactly two formant columns (F1 and F2) and a vowel column that identifies the phonemic
category of each observation. It is vowel-intrinsic (normalization depends on specific vowel categories) and
speaker-intrinsic.

.. note::

   Every speaker in the dataset must have at least one token of each point vowel (/i/, /a/, /u/). If a
   speaker is missing tokens for any of these vowels, the normalization will fail with an error message
   identifying the speaker.


Choosing a method
-----------------

The choice of normalization method depends on the research question and the nature of the data. Here are
some general guidelines:

- **Lobanov** is the most widely used method and a safe default for most sociophonetic studies. It is
  effective at removing speaker differences while preserving sociolinguistic variation.
- **Nearey 1** and **Nearey 2** are appropriate when you want to work in the log-frequency domain. Nearey 2
  is more constrained (uniform correction) and may be preferred when the assumption of a single
  vocal-tract-length factor is reasonable.
- **Watt & Fabricius** is useful when you have reliable point vowel data and want a normalization that is
  grounded in the geometry of the vowel space. It is particularly common in studies of vowel chain shifts.

For a detailed comparison of these and other methods, see Adank et al. (2004) and Flynn (2011).


References
----------

- Adank, Patti, Roel Smits & Roeland van Hout (2004). A comparison of vowel normalization procedures for
  language variation research. *Journal of the Acoustical Society of America*, 116(5), 3099–3107.
- Flynn, Nicholas (2011). Comparing vowel formant normalisation procedures. *York Papers in Linguistics*,
  Series 2(11), 1–28.
- Lobanov, Boris M. (1971). Classification of Russian vowels spoken by different speakers. *Journal of the
  Acoustical Society of America*, 49(2B), 606–608.
- Nearey, Terrance M. (1978). *Phonetic feature systems for vowels*. Indiana University Linguistics Club.
- Watt, Dominic & Anne Fabricius (2002). Evaluation of a technique for improving the mapping of multiple
  speakers' vowel spaces in the F1~F2 plane. *Leeds Working Papers in Linguistics and Phonetics*, 9,
  159–173.


.. |normvowel| image:: ../icons/vowel-space.svg
    :height: 16px
    :width: 16px
