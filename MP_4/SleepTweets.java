import java.io.IOException;
import java.lang.String;
import java.util.StringTokenizer;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;

public class SleepTweets {
    public static class SleepMapper extends Mapper<Object, Text, Text, IntWritable> {

    private final static IntWritable one = new IntWritable(1);
    private Text word = new Text();

    public void map(Object key, Text value, Context context) throws IOException, InterruptedException {
        StringTokenizer itr = new StringTokenizer(value.toString(),"\n");
        String hour = "";

        while (itr.hasMoreTokens()) {
            //get hour
            String line = itr.nextToken();
            if (line.length() != 0 && line.charAt(0)=='T'){
                StringTokenizer tk = new StringTokenizer(line);
                if (tk.hasMoreTokens()) tk.nextToken();
                if (tk.hasMoreTokens()) tk.nextToken();
                if (tk.hasMoreTokens()){
                    String timestamp = tk.nextToken();
                    hour = timestamp.substring(0,2);
                }
            }
            
            // throw away username line
            if (itr.hasMoreTokens()) itr.nextToken();
            if (itr.hasMoreTokens()){
                //decide if post has sleep in it
                String postLine = itr.nextToken();
                    if (postLine.length() != 0 && postLine.charAt(0)=='W'){
                        StringTokenizer ptk = new StringTokenizer(postLine);
                        if (ptk.hasMoreTokens()) ptk.nextToken();
                        while (ptk.hasMoreTokens()){
                            String post = ptk.nextToken();
                            if (post.indexOf("sleep")!= -1){
                                word.set(hour);
                                context.write(word, one);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    public static class SleepReducer extends Reducer<Text, IntWritable, Text, IntWritable> {
        private IntWritable sumWrite = new IntWritable();

        public void reduce(Text key, Iterable<IntWritable> values, Context context) throws IOException, InterruptedException {
            int sum = 0;
            for (IntWritable v : values) {
                sum += v.get();
            }
            sumWrite.set(sum);
            context.write(key, sumWrite);
        }
    }

    public static void main(String[] args) throws Exception {
        Configuration conf = new Configuration();
        conf.set("textinputformat.record.delimiter","\n\n");
        Job job = Job.getInstance(conf, "sleepy tweets per hour");
        job.setJarByClass(SleepTweets.class);
        job.setMapperClass(SleepMapper.class);
        job.setReducerClass(SleepReducer.class);
        job.setOutputKeyClass(Text.class);
        job.setOutputValueClass(IntWritable.class);
        FileInputFormat.addInputPath(job, new Path(args[0]));
        FileOutputFormat.setOutputPath(job, new Path(args[1]));
        System.exit(job.waitForCompletion(true) ? 0 : 1);
    }
}
